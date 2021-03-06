/*
 * This file is part of the PulseView project.
 *
 * Copyright (C) 2012-14 Joel Holdsworth <joel@airwebreathe.org.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifdef _WIN32
// Windows: Avoid boost/thread namespace pollution (which includes windows.h).
#define NOGDI
#define NORESOURCE
#endif
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <libsigrokdecode/libsigrokdecode.h>
#include "session.hpp"

#include "devicemanager.hpp"

#include "data/analog.hpp"
#include "data/analogsegment.hpp"
#include "data/decoderstack.hpp"
#include "data/logic.hpp"
#include "data/logicsegment.hpp"
#include "data/decode/decoder.hpp"

#include "devices/hardwaredevice.hpp"
#include "devices/sessionfile.hpp"

#include "view/analogsignal.hpp"
#include "view/decodetrace.hpp"
#include "view/logicsignal.hpp"

#include <cassert>
#include <mutex>
#include <stdexcept>

#include <sys/stat.h>

#include <QDebug>

#include <libsigrokcxx/libsigrokcxx.hpp>

using boost::shared_lock;
using boost::shared_mutex;
using boost::unique_lock;

using std::dynamic_pointer_cast;
using std::function;
using std::lock_guard;
using std::list;
using std::map;
using std::mutex;
using std::recursive_mutex;
using std::set;
using std::shared_ptr;
using std::string;
using std::unordered_set;
using std::vector;

using sigrok::Analog;
using sigrok::Channel;
using sigrok::ChannelType;
using sigrok::ConfigKey;
using sigrok::DatafeedCallbackFunction;
using sigrok::Error;
using sigrok::Header;
using sigrok::Logic;
using sigrok::Meta;
using sigrok::Packet;
using sigrok::PacketPayload;
using sigrok::Session;
using sigrok::SessionDevice;

using Glib::VariantBase;
using Glib::Variant;

namespace pv {
Session::Session(DeviceManager &device_manager) :
	device_manager_(device_manager),
	capture_state_(Stopped),
	cur_samplerate_(0),
	buffersize_(0),
	timeSpan(0),
	timespanLimitStream(0)
{
}

Session::~Session()
{
	// Stop and join to the thread
	stop_capture();
}

DeviceManager& Session::device_manager()
{
	return device_manager_;
}

const DeviceManager& Session::device_manager() const
{
	return device_manager_;
}

shared_ptr<sigrok::Session> Session::session() const
{
	if (!device_)
		return shared_ptr<sigrok::Session>();
	return device_->session();
}

shared_ptr<devices::Device> Session::device() const
{
	return device_;
}

void Session::set_device(shared_ptr<devices::Device> device)
{
	assert(device);

	// Ensure we are not capturing before setting the device
	stop_capture();

	if (device_)
		device_->close();

	device_.reset();

	// Remove all traces
	signals_.clear();
	cur_logic_segment_.reset();

	for (auto entry : cur_analog_segments_) {
		shared_ptr<sigrok::Channel>(entry.first).reset();
		shared_ptr<data::AnalogSegment>(entry.second).reset();
	}

	logic_data_.reset();
	decode_traces_.clear();

	signals_changed();

	device_ = std::move(device);
	device_->open();
	device_->session()->add_datafeed_callback([=]
		(shared_ptr<sigrok::Device> device, shared_ptr<Packet> packet) {
			data_feed_in(device, packet);
		});

	update_signals();
	device_selected();
}

void Session::set_default_device()
{
	const list< shared_ptr<devices::HardwareDevice> > &devices =
		device_manager_.devices();

	if (devices.empty())
		return;

	// Try and find the demo device and select that by default
	const auto iter = std::find_if(devices.begin(), devices.end(),
		[] (const shared_ptr<devices::HardwareDevice> &d) {
			return d->hardware_device()->driver()->name() ==
			"demo";	});
	set_device((iter == devices.end()) ? devices.front() : *iter);
}

Session::capture_state Session::get_capture_state() const
{
	lock_guard<mutex> lock(sampling_mutex_);
	return capture_state_;
}

void Session::start_capture(function<void (const QString)> error_handler)
{
	stop_capture();

	// Check that at least one channel is enabled
	assert(device_);
	const shared_ptr<sigrok::Device> sr_dev = device_->device();
	if (sr_dev) {
		const auto channels = sr_dev->channels();
		if (!std::any_of(channels.begin(), channels.end(),
			[](shared_ptr<Channel> channel) {
				return channel->enabled(); })) {
			error_handler(tr("No channels enabled."));
			return;
		}
	}

	// Clear signal data
	for (const shared_ptr<data::SignalData> d : get_data())
		d->clear();

	// Begin the session
	sampling_thread_ = std::thread(
		&Session::sample_thread_proc, this, device_,
			error_handler);
}

void Session::stop_capture()
{
	if (get_capture_state() != Stopped)
		device_->stop();

	// Check that sampling stopped
	if (sampling_thread_.joinable())
		sampling_thread_.join();

	// Clear signal data
	for (const shared_ptr<data::SignalData> d : get_data())
		d->clear_old_data();
}

set< shared_ptr<data::SignalData> > Session::get_data() const
{
	shared_lock<shared_mutex> lock(signals_mutex_);
	set< shared_ptr<data::SignalData> > data;
	for (const shared_ptr<view::Signal> sig : signals_) {
		assert(sig);
		data.insert(sig->data());
	}

	return data;
}

double Session::get_samplerate() const
{
	double samplerate = 0.0;

	for (const shared_ptr<pv::data::SignalData> d : get_data()) {
		assert(d);
		const vector< shared_ptr<pv::data::Segment> > segments =
			d->segments();
		for (const shared_ptr<pv::data::Segment> &s : segments)
			samplerate = std::max(samplerate, s->samplerate());
	}

	// If there is no sample rate given we use samples as unit
	if (samplerate == 0.0)
		samplerate = 1.0;

	return samplerate;
}

const unordered_set< shared_ptr<view::Signal> > Session::signals() const
{
	shared_lock<shared_mutex> lock(signals_mutex_);
	return signals_;
}

shared_ptr<pv::view::DecodeTrace> Session::add_decoder()
{
	shared_ptr<data::DecoderStack> decoder_stack;
	try {
		/* Create the decoder */
		decoder_stack = shared_ptr<data::DecoderStack>(
			new data::DecoderStack(*this));

		/* Create an empty decoder stack */
		shared_ptr<view::DecodeTrace> d(
			new view::DecodeTrace(*this, decoder_stack,
				decode_traces_.size()));


		decode_traces_.push_back(d);

		signals_changed();
	} catch (std::runtime_error e) {
		return NULL;
	}
	return decode_traces_.back();
}

void Session::remove_decode_clones()
{
	for (auto item = decode_traces_.begin(); item != decode_traces_.end(); )
		item = decode_traces_.erase(item);
	signals_changed();
}

void Session::remove_signal_clones()
{
	for (auto item = signals_.begin(); item != signals_.end(); )
		item = signals_.erase(item);
	signals_changed();
}

void Session::add_signal(shared_ptr<view::LogicSignal> sig)
{
	signals_.insert((shared_ptr<view::Signal>)(sig));
	signals_changed();
}
 
bool Session::add_decoder(srd_decoder *const dec)
{
	map<const srd_channel*, shared_ptr<view::LogicSignal> > channels;
	shared_ptr<data::DecoderStack> decoder_stack;

	try {
		lock_guard<boost::shared_mutex> lock(signals_mutex_);

		// Create the decoder
		decoder_stack = shared_ptr<data::DecoderStack>(
			new data::DecoderStack(*this, dec));

		// Make a list of all the channels
		std::vector<const srd_channel*> all_channels;
		for (const GSList *i = dec->channels; i; i = i->next)
			all_channels.push_back((const srd_channel*)i->data);
		for (const GSList *i = dec->opt_channels; i; i = i->next)
			all_channels.push_back((const srd_channel*)i->data);

		// Auto select the initial channels
		for (const srd_channel *pdch : all_channels)
			for (shared_ptr<view::Signal> s : signals_) {
				shared_ptr<view::LogicSignal> l =
					dynamic_pointer_cast<view::LogicSignal>(s);
				if (l && QString::fromUtf8(pdch->name).
					toLower().contains(
					l->name().toLower()))
					channels[pdch] = l;
			}

		assert(decoder_stack);
		assert(!decoder_stack->stack().empty());
		assert(decoder_stack->stack().front());
		decoder_stack->stack().front()->set_channels(channels);

		// Create the decode signal
		shared_ptr<view::DecodeTrace> d(
			new view::DecodeTrace(*this, decoder_stack,
				decode_traces_.size()));
		decode_traces_.push_back(d);
	} catch (std::runtime_error e) {
		return false;
	}

	signals_changed();

	// Do an initial decode
	decoder_stack->begin_decode();

	return true;
}

vector< shared_ptr<view::DecodeTrace> > Session::get_decode_signals() const
{
	shared_lock<shared_mutex> lock(signals_mutex_);
	return decode_traces_;
}

void Session::remove_decode_signal(view::DecodeTrace *signal)
{
	for (auto i = decode_traces_.begin(); i != decode_traces_.end(); i++)
		if ((*i).get() == signal) {
			decode_traces_.erase(i);
			signals_changed();
			return;
		}
}

void Session::set_capture_state(capture_state state)
{
	bool changed;

	{
		lock_guard<mutex> lock(sampling_mutex_);
		changed = capture_state_ != state;
		capture_state_ = state;
	}

	if (changed)
		capture_state_changed(state);
}

void Session::set_buffersize(size_t value)
{
	buffersize_ = value;
}

void Session::set_samplerate(double value)
{
	cur_samplerate_ = value;
}

void Session::set_timeSpan(double value)
{
	timeSpan = value;
}

void Session::set_timespanLimit(double value)
{
	timespanLimitStream = value;
}

shared_ptr<view::LogicSignal> Session::create_signal_from_id(int index, int height)
{
    const shared_ptr<sigrok::Device> sr_dev = device_->device();
    if (!sr_dev) {
        signals_.clear();
        logic_data_.reset();
        return nullptr;
    }

    shared_ptr<view::LogicSignal> signal;
    for (shared_ptr<sigrok::Channel> channel : sr_dev->channels()) {
        if(channel->index() == index){
	    signal = shared_ptr<view::LogicSignal>(
                new view::LogicSignal(*this,
                    device_, channel,
		    logic_data_,
				      height));
            break;
        }
    }
    assert(signal);
    //assert(signal->owner());
    signals_.insert(signal);
    signals_changed();
    return signal;
}


void Session::update_signals()
{
	if (!device_) {
		signals_.clear();
		logic_data_.reset();
		return;
	}

	lock_guard<recursive_mutex> lock(data_mutex_);

	const shared_ptr<sigrok::Device> sr_dev = device_->device();
	if (!sr_dev) {
		signals_.clear();
		logic_data_.reset();
		return;
	}

	// Detect what data types we will receive
	auto channels = sr_dev->channels();
	unsigned int logic_channel_count = std::count_if(
		channels.begin(), channels.end(),
		[] (shared_ptr<Channel> channel) {
			return channel->type() == ChannelType::LOGIC; });

	// Create data containers for the logic data segments
	{
		lock_guard<recursive_mutex> data_lock(data_mutex_);

		if (logic_channel_count == 0) {
			logic_data_.reset();
		} else if (!logic_data_ ||
			logic_data_->num_channels() != logic_channel_count) {
			logic_data_.reset(new data::Logic(
				logic_channel_count));
			assert(logic_data_);
		}
	}

	// Make the Signals list
	{
		unique_lock<shared_mutex> lock(signals_mutex_);

		unordered_set< shared_ptr<view::Signal> > prev_sigs(signals_);
		signals_.clear();

		for (auto channel : sr_dev->channels()) {
			shared_ptr<view::Signal> signal;

			// Find the channel in the old signals
			const auto iter = std::find_if(
				prev_sigs.cbegin(), prev_sigs.cend(),
				[&](const shared_ptr<view::Signal> &s) {
					return s->channel() == channel;
				});
			if (iter != prev_sigs.end()) {
				// Copy the signal from the old set to the new
				signal = *iter;
				auto logic_signal = dynamic_pointer_cast<
					view::LogicSignal>(signal);
				if (logic_signal)
					logic_signal->set_logic_data(
						logic_data_);
			} else {
				// Create a new signal
				switch(channel->type()->id()) {
				case SR_CHANNEL_LOGIC:
					signal = shared_ptr<view::Signal>(
						new view::LogicSignal(*this,
							device_, channel,
							logic_data_));
					break;

				case SR_CHANNEL_ANALOG:
				{
					shared_ptr<data::Analog> data(
						new data::Analog());
					signal = shared_ptr<view::Signal>(
						new view::AnalogSignal(
							*this, channel, data));
					break;
				}

				default:
					assert(0);
					break;
				}
			}

			assert(signal);
			signals_.insert(signal);
		}
	}

	signals_changed();
}

shared_ptr<view::Signal> Session::signal_from_channel(
	shared_ptr<Channel> channel) const
{
	lock_guard<boost::shared_mutex> lock(signals_mutex_);
	for (shared_ptr<view::Signal> sig : signals_) {
		assert(sig);
		if (sig->channel() == channel)
			return sig;
	}
	return shared_ptr<view::Signal>();
}

void Session::sample_thread_proc(shared_ptr<devices::Device> device,
	function<void (const QString)> error_handler)
{
	assert(device);
	assert(error_handler);

	(void)device;

	if(timespanLimitStream == 0)
		cur_samplerate_ = device_->read_config<uint64_t>(ConfigKey::SAMPLERATE);

	out_of_memory_ = false;

	try {
		device_->start();
	} catch (Error e) {
		error_handler(e.what());
		return;
	}

	set_capture_state(device_->session()->trigger() ?
		AwaitingTrigger : Running);

	device_->run();
	set_capture_state(Stopped);

	// Confirm that SR_DF_END was received
	if (cur_logic_segment_) {
		qDebug("SR_DF_END was not received.");
		assert(0);
	}

	if (out_of_memory_)
		error_handler(tr("Out of memory, acquisition stopped."));
}

void Session::feed_in_header()
{
	if(timespanLimitStream == 0)
		cur_samplerate_ = device_->read_config<uint64_t>(ConfigKey::SAMPLERATE);
}

void Session::feed_in_meta(shared_ptr<Meta> meta)
{
	for (auto entry : meta->config()) {
		switch (entry.first->id()) {
		case SR_CONF_SAMPLERATE:
			// We can't rely on the header to always contain the sample rate,
			// so in case it's supplied via a meta packet, we use it.
			if (!cur_samplerate_)
				cur_samplerate_ = g_variant_get_uint64(entry.second.gobj());

			/// @todo handle samplerate changes
			break;
		default:
			// Unknown metadata is not an error.
			break;
		}
	}

	signals_changed();
}

void Session::feed_in_trigger()
{
	// The channel containing most samples should be most accurate
	uint64_t sample_count = 0;

	for (const shared_ptr<pv::data::SignalData> d : get_data()) {
		assert(d);
		uint64_t temp_count = 0;

		const vector< shared_ptr<pv::data::Segment> > segments =
			d->segments();
		for (const shared_ptr<pv::data::Segment> &s : segments)
			temp_count += s->get_sample_count();

		if (temp_count > sample_count)
			sample_count = temp_count;
	}

	trigger_event(sample_count / get_samplerate());
}

void Session::feed_in_frame_begin()
{
	if (cur_logic_segment_ || !cur_analog_segments_.empty())
		frame_began();
}

void Session::feed_in_logic(shared_ptr<Logic> logic)
{
	lock_guard<recursive_mutex> lock(data_mutex_);

	const size_t sample_count = logic->data_length() / logic->unit_size();

	if (!logic_data_) {
		// The only reason logic_data_ would not have been created is
		// if it was not possible to determine the signals when the
		// device was created.
		update_signals();
	}

	if (!cur_logic_segment_) {
		// This could be the first packet after a trigger
		set_capture_state(Running);

		// Create a new data segment
		cur_logic_segment_ = shared_ptr<data::LogicSegment>(
			new data::LogicSegment(
				logic, cur_samplerate_, sample_count));
		logic_data_->push_segment(cur_logic_segment_);

		// @todo Putting this here means that only listeners querying
		// for logic will be notified. Currently the only user of
		// frame_began is DecoderStack, but in future we need to signal
		// this after both analog and logic sweeps have begun.
		frame_began();
	} else {
		// Append to the existing data segment
		cur_logic_segment_->append_payload(logic);
	}

	data_received();
}

bool Session::is_data()
{
	if(logic_data_->segments().size() == 0)
		return false;
	return true;
}

void Session::feed_in_analog(shared_ptr<Analog> analog)
{
	lock_guard<recursive_mutex> lock(data_mutex_);

	const vector<shared_ptr<Channel>> channels = analog->channels();
	const unsigned int channel_count = channels.size();
	const size_t sample_count = analog->num_samples() / channel_count;
	const float *data = static_cast<const float *>(analog->data_pointer());
	bool sweep_beginning = false;

	if (signals_.empty())
		update_signals();

	for (auto channel : channels) {
		shared_ptr<data::AnalogSegment> segment;

		// Try to get the segment of the channel
		const map< shared_ptr<Channel>, shared_ptr<data::AnalogSegment> >::
			iterator iter = cur_analog_segments_.find(channel);
		if (iter != cur_analog_segments_.end())
			segment = (*iter).second;
		else {
			// If no segment was found, this means we haven't
			// created one yet. i.e. this is the first packet
			// in the sweep containing this segment.
			sweep_beginning = true;

			// Create a segment, keep it in the maps of channels
			segment = shared_ptr<data::AnalogSegment>(
				new data::AnalogSegment(
					cur_samplerate_, sample_count));
			cur_analog_segments_[channel] = segment;

			// Find the analog data associated with the channel
			shared_ptr<view::AnalogSignal> sig =
				dynamic_pointer_cast<view::AnalogSignal>(
					signal_from_channel(channel));
			assert(sig);

			shared_ptr<data::Analog> data(sig->analog_data());
			assert(data);

			// Push the segment into the analog data.
			data->push_segment(segment);
		}

		assert(segment);

		// Append the samples in the segment
		segment->append_interleaved_samples(data++, sample_count,
			channel_count);
	}

	if (sweep_beginning) {
		// This could be the first packet after a trigger
		set_capture_state(Running);
	}

	data_received();
}

void Session::data_feed_in(shared_ptr<sigrok::Device> device,
	shared_ptr<Packet> packet)
{
	(void)device;

	assert(device);
	assert(device == device_->device());
	assert(packet);

	switch (packet->type()->id()) {
	case SR_DF_HEADER:
		feed_in_header();
		break;

	case SR_DF_META:
		feed_in_meta(dynamic_pointer_cast<Meta>(packet->payload()));
		break;

	case SR_DF_TRIGGER:
		feed_in_trigger();
		break;

	case SR_DF_FRAME_BEGIN:
		feed_in_frame_begin();
		break;

	case SR_DF_LOGIC:
		try {
			feed_in_logic(dynamic_pointer_cast<Logic>(packet->payload()));
		} catch (std::bad_alloc) {
			out_of_memory_ = true;
			device_->stop();
		}
		break;

	case SR_DF_ANALOG:
		try {
			feed_in_analog(dynamic_pointer_cast<Analog>(packet->payload()));
		} catch (std::bad_alloc) {
			out_of_memory_ = true;
			device_->stop();
		}
		break;

	case SR_DF_END:
	{
		{
			lock_guard<recursive_mutex> lock(data_mutex_);
			cur_logic_segment_.reset();
			cur_analog_segments_.clear();
		}
		frame_ended();
		break;
	}
	default:
		break;
	}
}

} // namespace pv
