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

#ifndef M2K_TOOL_LAUNCHER_H
#define M2K_TOOL_LAUNCHER_H

#include <QFuture>
#include <QFutureWatcher>
#include <QJSEngine>
#include <QMainWindow>
#include <QPair>
#include <QSocketNotifier>
#include <QVector>
#include <QButtonGroup>
#include <QMap>
#include <QStringList>

#include "apiObject.hpp"
#include "dmm.hpp"
#include "filter.hpp"
#include "calibration.hpp"
#include "oscilloscope.hpp"
#include "pulseview/pv/widgets/popup.hpp"
#include "power_controller.hpp"
#include "signal_generator.hpp"
#include "logic_analyzer.hpp"
#include "pattern_generator.hpp"
#include "network_analyzer.hpp"
#include "digitalio.hpp"
#include "menuoption.h"

extern "C" {
	struct iio_context;
}

namespace Ui {
class ToolLauncher;
class Device;
class GenericAdc;
}

namespace adiscope {
class ToolLauncher_API;
class SpectrumAnalyzer;

class ToolLauncher : public QMainWindow
{
	friend class ToolLauncher_API;

	Q_OBJECT

public:
	explicit ToolLauncher(QWidget *parent = 0);
	~ToolLauncher();

	Q_INVOKABLE void runProgram(const QString& program, const QString& fn);

	//QMap<QString, MenuOption*> getMenuOptions();

Q_SIGNALS:
	void connectionDone(bool success);
	void adcCalibrationDone();
	void dacCalibrationDone(float dacA_vlsb, float dacB_vlsb);

	void adcToolsCreated();
	void dacToolsCreated();

private Q_SLOTS:
	void search();
	void update();
	void ping();

	void btnOscilloscope_clicked();
	void btnSignalGenerator_clicked();
	void btnDMM_clicked();
	void btnPowerControl_clicked();
	void btnLogicAnalyzer_clicked();
	void btnPatternGenerator_clicked();
	void btnNetworkAnalyzer_clicked();
	void btnSpectrumAnalyzer_clicked();
	void on_btnHome_clicked();
	void setButtonBackground(bool on);

	void on_btnConnect_clicked(bool pressed);

	void device_btn_clicked(bool pressed);
	void addRemoteContext();
	void destroyPopup();

	void enableAdcBasedTools();
	void enableDacBasedTools(float dacA_vlsb, float dacB_vlsb);

	void hasText();

	void btnDigitalIO_clicked();

	void toolDetached(bool detached);

	void swapMenuOptions(int source, int destination, bool dropAfter);
	void highlight(bool on, int position);

private:
	Ui::ToolLauncher *ui;
	struct iio_context *ctx;

	QMap<QString, MenuOption*> toolMenu;

	QVector<QPair<QWidget, Ui::Device> *> devices;

	QTimer *search_timer, *alive_timer;
	QFutureWatcher<QVector<QString>> watcher;
	QFuture<QVector<QString>> future;

	DMM *dmm;
	PowerController *power_control;
	SignalGenerator *signal_generator;
	Oscilloscope *oscilloscope;
	LogicAnalyzer *logic_analyzer;
	DigitalIO *dio;
	DIOManager *dioManager;
	PatternGenerator *pattern_generator;
	NetworkAnalyzer *network_analyzer;
	SpectrumAnalyzer *spectrum_analyzer;
	QWidget *current;
	QSettings *settings;

	QButtonGroup adc_users_group;

	std::shared_ptr<GenericAdc> adc;

	Filter *filter;
	ToolLauncher_API *tl_api;

	QJSEngine js_engine;
	QString js_cmd;
	QSocketNotifier notifier;
	QString previousIp;

	void loadToolTips(bool connected);
	QVector<QString> searchDevices();
	void swapMenu(QWidget *menu);
	void destroyContext();
	bool loadDecoders(QString path);
	bool switchContext(const QString& uri);
	void resetStylesheets();
	void calibrate();
	void checkIp(const QString& ip);
	void disconnect();
	void saveSettings();
	Q_INVOKABLE QPushButton *addContext(const QString& hostname);

	void updateListOfDevices(const QVector<QString>& uris);
	void generateMenu();
	QStringList tools;
	QStringList toolIcons;
	void UpdatePosition(QWidget *widget, int position);
};

class ToolLauncher_API: public ApiObject
{
	Q_OBJECT

	Q_PROPERTY(bool menu_opened READ menu_opened WRITE open_menu
	           STORED false);

	Q_PROPERTY(bool hidden READ hidden WRITE hide STORED false);

	Q_PROPERTY(QString previous_ip READ getPreviousIp WRITE addIp
	           SCRIPTABLE false);

	Q_PROPERTY(bool maximized READ maximized WRITE setMaximized);

public:
	explicit ToolLauncher_API(ToolLauncher *tl) : ApiObject(), tl(tl) {}
	~ToolLauncher_API() {}

	bool menu_opened() const;
	void open_menu(bool open);

	bool hidden() const;
	void hide(bool hide);

	const QString& getPreviousIp()
	{
		return tl->previousIp;
	}
	void addIp(const QString& ip);

	bool maximized()
	{
		return tl->isMaximized();
	}
	void setMaximized(bool m)
	{
		if (m) {
			tl->showMaximized();
		} else {
			tl->showNormal();
		}
	}

	Q_INVOKABLE bool connect(const QString& uri);
	Q_INVOKABLE void disconnect();

	Q_INVOKABLE void load(const QString& file);
	Q_INVOKABLE void save(const QString& file);

private:
	ToolLauncher *tl;
};
}

#endif // M2K_TOOL_LAUNCHER_H
