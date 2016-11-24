#ifndef OSC_ADC_H
#define OSC_ADC_H

#include <QList>
#include <QStringList>
#include <map>

extern "C" {
	struct iio_context;
	struct iio_device;
	struct iio_channel;
}

namespace adiscope {

	class Filter;

	class OscADC
	{
	public:
		OscADC(struct iio_context *, const Filter *);
		~OscADC();

		double sampleRate() const;
		void setSampleRate(double);
		QList<double> availSamplRates() const;
		unsigned int numChannels() const;

		struct iio_device *iio_adc() const;

		double compTable(double samplRate) const;

	private:
		static unsigned int get_nb_channels(struct iio_device *dev);
		static QStringList get_avail_options_list(struct iio_device *dev,
					const char *attr_name);

	private:
		struct iio_context *m_ctx;
		struct iio_device *m_adc;
		double m_sampleRate;
		QList<double > m_availSampRates;
		unsigned int m_numChannels;

		std::map<double, double> m_filt_comp_table;
	};
}

#endif // OSC_ADC_H
