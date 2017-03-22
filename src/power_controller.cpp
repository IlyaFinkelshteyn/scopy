/*
 * Copyright 2016 Analog Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file LICENSE.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "power_controller.hpp"
#include "filter.hpp"

#include "ui_powercontrol.h"

#include <iio.h>

#define TIMER_TIMEOUT_MS	200

using namespace adiscope;

PowerController::PowerController(struct iio_context *ctx,
		QPushButton *runButton, QJSEngine *engine, QWidget *parent) :
	QWidget(parent), ui(new Ui::PowerController),
	in_sync(false), menuRunButton(runButton),
	pw_api(new PowerController_API(this))
{
	ui->setupUi(this);

	struct iio_device *dev1 = iio_context_find_device(ctx, "ad5627");
	struct iio_device *dev2 = iio_context_find_device(ctx, "ad9963");
	if (!dev1 || !dev2)
		throw std::runtime_error("Unable to find device\n");

	this->ch1w = iio_device_find_channel(dev1, "voltage0", true);
	this->ch2w = iio_device_find_channel(dev1, "voltage1", true);
	this->ch1r = iio_device_find_channel(dev2, "voltage2", false);
	this->ch2r = iio_device_find_channel(dev2, "voltage1", false);
	if (!ch1w || !ch2w || !ch1r || !ch2r)
		throw std::runtime_error("Unable to find channels\n");

	/* Power down DACs by default */
	iio_channel_attr_write_bool(ch1w, "powerdown", true);
	iio_channel_attr_write_bool(ch2w, "powerdown", true);

	/* Set the default values */
	iio_channel_attr_write_longlong(ch1w, "raw", 0LL);
	iio_channel_attr_write_longlong(ch2w, "raw", 0LL);

	connect(&this->timer, SIGNAL(timeout()), this, SLOT(update_lcd()));

	connect(ui->dac1, SIGNAL(toggled(bool)), this,
			SLOT(dac1_set_enabled(bool)));
	connect(ui->dac2, SIGNAL(toggled(bool)), this,
			SLOT(dac2_set_enabled(bool)));
	connect(ui->sync, SIGNAL(toggled(bool)), this,
			SLOT(sync_enabled(bool)));

	auto layout = static_cast<QVBoxLayout *>(ui->rightMenu->layout());

	valuePos = new PositionSpinButton({
			{"mVolts", 1E-3},
			{"Volts", 1E0}
			}, "Value", 0.0, 5e0);
	layout->insertWidget(8, valuePos, 0, Qt::AlignLeft);

	connect(valuePos, SIGNAL(valueChanged(double)), this,
			SLOT(dac1_set_value(double)));
	connect(valuePos, SIGNAL(valueChanged(double)), ui->lcd1_set,
			SLOT(display(double)));

	valueNeg = new PositionSpinButton({
			{"mVolts", 1E-3},
			{"Volts", 1E0}
			}, "Value", -5e0, 0.0, true, true);
	layout->insertWidget(13, valueNeg, 0, Qt::AlignLeft);

	connect(ui->sync, SIGNAL(toggled(bool)), valueNeg,
			SLOT(setDisabled(bool)));
	connect(valueNeg, SIGNAL(valueChanged(double)), this,
			SLOT(dac2_set_value(double)));
	connect(valueNeg, SIGNAL(valueChanged(double)), ui->lcd2_set,
			SLOT(display(double)));

	connect(ui->trackingRatio, SIGNAL(valueChanged(int)), this,
			SLOT(ratioChanged(int)));

	connect(runButton, SIGNAL(clicked(bool)), this, SLOT(startStop(bool)));

	timer.start(TIMER_TIMEOUT_MS);

	pw_api->setObjectName(QString::fromStdString(Filter::tool_name(
			TOOL_POWER_CONTROLLER)));
	pw_api->load();
	pw_api->js_register(engine);
}

PowerController::~PowerController()
{
	/* Power down DACs */
	iio_channel_attr_write_bool(ch1w, "powerdown", true);
	iio_channel_attr_write_bool(ch2w, "powerdown", true);

	pw_api->save();
	delete pw_api;

	delete ui;
}

void PowerController::dac1_set_value(double value)
{
	long long val = value * 4095.0 / (5.02 * 1.2);

	iio_channel_attr_write_longlong(ch1w, "raw", val);

	if (in_sync) {
		value = -value * ui->trackingRatio->value() / 100.0;
		valueNeg->setValue(value);
		dac2_set_value(value);
	}
}

void PowerController::dac2_set_value(double value)
{
	long long val = value * 4095.0 / (-5.1 * 1.2);

	iio_channel_attr_write_longlong(ch2w, "raw", val);
}

void PowerController::dac1_set_enabled(bool enabled)
{
	iio_channel_attr_write_bool(ch1w, "powerdown", !enabled);

	if (in_sync)
		dac2_set_enabled(enabled);

	if (enabled)
		menuRunButton->setChecked(true);
	else if (!ui->dac2->isChecked())
		menuRunButton->setChecked(false);
}

void PowerController::dac2_set_enabled(bool enabled)
{
	iio_channel_attr_write_bool(ch2w, "powerdown", !enabled);

	if (enabled)
		menuRunButton->setChecked(true);
	else if (!ui->dac1->isChecked())
		menuRunButton->setChecked(false);
}

void PowerController::sync_enabled(bool enabled)
{
	if (ui->dac1->isChecked()) {
		dac2_set_enabled(enabled);
		ui->dac2->setChecked(enabled);
	}

	in_sync = enabled;
	valueNeg->setDisabled(enabled);
	valueNeg->setValue(-valuePos->value() *
			(double) ui->trackingRatio->value() / 100.0);
}

void PowerController::ratioChanged(int percent)
{
	valueNeg->setValue(-valuePos->value() *
			(double) percent / 100.0);
}

void PowerController::update_lcd()
{
	long long val1 = 0, val2 = 0;

	iio_channel_attr_read_longlong(ch1r, "raw", &val1);
	iio_channel_attr_read_longlong(ch2r, "raw", &val2);

	double value1 = (double) val1 * 6.4 / 4095.0;
	ui->lcd1->display(value1);
	ui->scale_dac1->setValue(value1);

	double value2 = (double) val2 * -6.4 / 4095.0;
	ui->lcd2->display(value2);
	ui->scale_dac2->setValue(value2);

	timer.start(TIMER_TIMEOUT_MS);
}

void PowerController::startStop(bool start)
{
	dac1_set_enabled(start);
	ui->dac1->setChecked(start);

	dac2_set_enabled(start);
	ui->dac2->setChecked(start);
}

bool PowerController_API::syncEnabled() const
{
	return pw->ui->sync->isChecked();
}

void PowerController_API::enableSync(bool en)
{
	if (en)
		pw->ui->sync->click();
	else
		pw->ui->notSync->click();
}

int PowerController_API::getTrackingPercent() const
{
	return pw->ui->trackingRatio->value();
}

void PowerController_API::setTrackingPercent(int percent)
{
	pw->ui->trackingRatio->setValue(percent);
}

double PowerController_API::valueDac1() const
{
	return pw->valuePos->value();
}

void PowerController_API::setValueDac1(double value)
{
	pw->valuePos->setValue(value);
}

double PowerController_API::valueDac2() const
{
	return pw->valueNeg->value();
}

void PowerController_API::setValueDac2(double value)
{
	if (!syncEnabled())
		pw->valueNeg->setValue(value);
}
