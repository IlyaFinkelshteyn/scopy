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

#include "config.h"
#include "connectDialog.hpp"
#include "detachedWindow.hpp"
#include "dynamicWidget.hpp"
#include "oscilloscope.hpp"
#include "spectrum_analyzer.hpp"
#include "tool_launcher.hpp"
#include "qtjs.hpp"
#include "menuoption.h"
#include "menuoptioncustom.h"

#include "ui_device.h"
#include "ui_tool_launcher.h"

#include <QDebug>
#include <QtConcurrentRun>
#include <QSignalTransition>
#include <QMessageBox>
#include <QTimer>
#include <QSettings>
#include <QStringList>

#include <iio.h>

#define TIMER_TIMEOUT_MS 5000
#define ALIVE_TIMER_TIMEOUT_MS 5000
#define MAX_MENU_OPTIONS 9

using namespace adiscope;

ToolLauncher::ToolLauncher(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::ToolLauncher), ctx(nullptr),
	power_control(nullptr), dmm(nullptr), signal_generator(nullptr),
	oscilloscope(nullptr), current(nullptr), filter(nullptr),
	logic_analyzer(nullptr), pattern_generator(nullptr), dio(nullptr),
	network_analyzer(nullptr), spectrum_analyzer(nullptr),
	tl_api(new ToolLauncher_API(this)),
	notifier(STDIN_FILENO, QSocketNotifier::Read)
{
	if (!isatty(STDIN_FILENO))
		notifier.setEnabled(false);

	ui->setupUi(this);
	generateMenu();

	setWindowIcon(QIcon(":/icon.ico"));

	// TO DO: remove this when the About menu becomes available
	setWindowTitle(QString("Scopy - ") + QString(SCOPY_VERSION_GIT));

	const QVector<QString>& uris = searchDevices();
	for (const QString& each : uris)
		addContext(each);

	current = ui->homeWidget;

	ui->menu->setMinimumSize(ui->menu->sizeHint());

	connect(this, SIGNAL(adcCalibrationDone()),
		this, SLOT(enableAdcBasedTools()));
	connect(this, SIGNAL(dacCalibrationDone(float, float)),
		this, SLOT(enableDacBasedTools(float, float)));
	connect(ui->btnAdd, SIGNAL(clicked()), this, SLOT(addRemoteContext()));

	tl_api->setObjectName(QString::fromStdString(Filter::tool_name(
			TOOL_LAUNCHER)));

	/* Show a smooth opening when the app starts */
	ui->menu->toggleMenu(true);

	//option click
	connect(toolMenu["Oscilloscope"]->getToolBtn(), SIGNAL(clicked()), this,
		SLOT(btnOscilloscope_clicked()));
	connect(toolMenu["Signal Generator"]->getToolBtn(), SIGNAL(clicked()), this,
		SLOT(btnSignalGenerator_clicked()));
	connect(toolMenu["Voltmeter"]->getToolBtn(), SIGNAL(clicked()), this,
		SLOT(btnDMM_clicked()));
	connect(toolMenu["Power Supply"]->getToolBtn(), SIGNAL(clicked()), this,
		SLOT(btnPowerControl_clicked()));
	connect(toolMenu["Logic Analyzer"]->getToolBtn(), SIGNAL(clicked()), this,
		SLOT(btnLogicAnalyzer_clicked()));
	connect(toolMenu["Pattern Generator"]->getToolBtn(), SIGNAL(clicked()), this,
		SLOT(btnPatternGenerator_clicked()));
	connect(toolMenu["Network Analyzer"]->getToolBtn(), SIGNAL(clicked()), this,
		SLOT(btnNetworkAnalyzer_clicked()));
	connect(toolMenu["Digital IO"]->getToolBtn(), SIGNAL(clicked()), this,
		SLOT(btnDigitalIO_clicked()));
	connect(toolMenu["Spectrum Analyzer"]->getToolBtn(), SIGNAL(clicked()), this,
		SLOT(btnSpectrumAnalyzer_clicked()));


	//option background
	connect(toolMenu["Oscilloscope"]->getToolBtn(), SIGNAL(toggled(bool)), this,
		SLOT(setButtonBackground(bool)));
	connect(toolMenu["Signal Generator"]->getToolBtn(), SIGNAL(toggled(bool)), this,
		SLOT(setButtonBackground(bool)));
	connect(toolMenu["Voltmeter"]->getToolBtn(), SIGNAL(toggled(bool)), this,
		SLOT(setButtonBackground(bool)));
	connect(toolMenu["Power Supply"]->getToolBtn(), SIGNAL(toggled(bool)), this,
		SLOT(setButtonBackground(bool)));
	connect(toolMenu["Logic Analyzer"]->getToolBtn(), SIGNAL(toggled(bool)), this,
		SLOT(setButtonBackground(bool)));
	connect(toolMenu["Pattern Generator"]->getToolBtn(), SIGNAL(toggled(bool)), this,
		SLOT(setButtonBackground(bool)));
	connect(toolMenu["Network Analyzer"]->getToolBtn(), SIGNAL(toggled(bool)), this,
		SLOT(setButtonBackground(bool)));
	connect(toolMenu["Digital IO"]->getToolBtn(), SIGNAL(toggled(bool)), this,
		SLOT(setButtonBackground(bool)));
	connect(toolMenu["Spectrum Analyzer"]->getToolBtn(), SIGNAL(toggled(bool)), this,
		SLOT(setButtonBackground(bool)));
	connect(ui->btnHome, SIGNAL(toggled(bool)), this,
		SLOT(setButtonBackground(bool)));

	ui->btnHome->toggle();

	//setAcceptDrops(true);

	qDebug()<<toolMenu["Spectrum Analyzer"]->getToolBtn()->isEnabled();
	//qDebug()<<toolMenu["Spectrum Analyzer"]->ui;
	//toolMenu["Spectrum Analyzer"]->setEnabled(true);
	qDebug()<<toolMenu["Spectrum Analyzer"]->getToolBtn()->isEnabled();

	loadToolTips(false);

#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
	js_engine.installExtensions(QJSEngine::ConsoleExtension);
#endif
	QtJs *js_object = new QtJs(&js_engine);
	tl_api->js_register(&js_engine);

	connect(&notifier, SIGNAL(activated(int)), this, SLOT(hasText()));

	search_timer = new QTimer();
	connect(search_timer, SIGNAL(timeout()), this, SLOT(search()));
	connect(&watcher, SIGNAL(finished()), this, SLOT(update()));
	search_timer->start(TIMER_TIMEOUT_MS);

	alive_timer = new QTimer();
	connect(alive_timer, SIGNAL(timeout()), this, SLOT(ping()));

	QSettings oldSettings;
	QFile scopy(oldSettings.fileName());
	QFile tempFile(oldSettings.fileName() + ".bak");
	if (tempFile.exists())
		tempFile.remove();
	scopy.copy(tempFile.fileName());
	settings = new QSettings(tempFile.fileName(), QSettings::IniFormat);

	tl_api->ApiObject::load(*settings);


}

void ToolLauncher::saveSettings()
{
	QSettings settings;
	QFile tempFile(settings.fileName() + ".bak");
	QSettings tempSettings(tempFile.fileName(), QSettings::IniFormat);
	QFile scopyFile(settings.fileName());
	if (scopyFile.exists())
		scopyFile.remove();
	tempSettings.sync();
	QFile::copy(tempFile.fileName(), scopyFile.fileName());
}

void ToolLauncher::runProgram(const QString& program, const QString& fn)
{
	QJSValue val = js_engine.evaluate(program, fn);

	int ret;
	if (val.isError()) {
		qInfo() << "Exception:" << val.toString();
		ret = EXIT_FAILURE;
	} else if (!val.isUndefined()) {
		qInfo() << val.toString();
		ret = EXIT_SUCCESS;
	}

	/* Exit application */
	qApp->exit(ret);
}


void ToolLauncher::search()
{
	search_timer->stop();
	future = QtConcurrent::run(this, &ToolLauncher::searchDevices);
	watcher.setFuture(future);
}

QVector<QString> ToolLauncher::searchDevices()
{
	struct iio_context_info **info;
	unsigned int nb_contexts;
	QVector<QString> uris;

	struct iio_scan_context *scan_ctx = iio_create_scan_context("usb", 0);

	if (!scan_ctx) {
		std::cerr << "Unable to create scan context!" << std::endl;
		return uris;
	}

	ssize_t ret = iio_scan_context_get_info_list(scan_ctx, &info);

	if (ret < 0) {
		std::cerr << "Unable to scan!" << std::endl;
		goto out_destroy_context;
	}

	nb_contexts = static_cast<unsigned int>(ret);

	for (unsigned int i = 0; i < nb_contexts; i++)
		uris.append(QString(iio_context_info_get_uri(info[i])));

	iio_context_info_list_free(info);
out_destroy_context:
	iio_scan_context_destroy(scan_ctx);
	return uris;
}

void ToolLauncher::updateListOfDevices(const QVector<QString>& uris)
{
	//Delete devices that are in the devices list but not found anymore when scanning

	for (auto it = devices.begin(); it != devices.end();) {
		QString uri = (*it)->second.btn->property("uri").toString();

		if (uri.startsWith("usb:") && !uris.contains(uri)) {
			if ((*it)->second.btn->isChecked()){
				(*it)->second.btn->click();
				return;
			}
			delete *it;
			it = devices.erase(it);
		} else {
			++it;
		}
	}

	for (const QString& uri : uris) {
		if (!uri.startsWith("usb:"))
			continue;

		bool found = false;

		for (const auto each : devices) {
			QString str = each->second.btn->property("uri")
				.toString();

			if (str == uri) {
				found = true;
				break;
			}
		}

		if (!found)
			addContext(uri);
	}

	search_timer->start(TIMER_TIMEOUT_MS);
}

void ToolLauncher::generateMenu()
{
	tools << "Digital IO" << "Voltmeter"
		<< "Oscilloscope" << "Power Supply"
		<< "Signal Generator" << "Pattern Generator"
		<< "Logic Analyzer" << "Network Analyzer"
		<< "Spectrum Analyzer";

	toolIcons << ":/menu/io.png"
		<< ":/menu/voltmeter.png"
		<< ":/menu/oscilloscope.png"
		<< ":/menu/power_supply.png"
		<< ":/menu/signal_generator.png"
		<< ":/menu/pattern_generator.png"
		<< ":/menu/logic_analyzer.png"
		<< ":/menu/network_analyzer.png"
		<< ":/menu/spectrum_analyzer.png";
	for (int i = 0; i < tools.size(); ++i){
		if (tools[i] == "Oscilloscope" ||
			tools[i] == "Voltmeter" ||
			tools[i] == "Spectrum Analyzer" ||
			tools[i] == "Siganl Generator") {
			toolMenu.insert(tools[i],
					new MenuOption(tools[i],
						toolIcons[i], i, true, ui->menu));
		} else {
		toolMenu.insert(tools[i],
				new MenuOption(tools[i],
						toolIcons[i], i, false, ui->menu));
		}
		connect(toolMenu[tools[i]], SIGNAL(requestPositionChange(int, int, bool)), this,
				SLOT(swapMenuOptions(int, int, bool)));
		ui->menuOptionsLayout->insertWidget(i, toolMenu[tools[i]]);
		ui->buttonGroup_2->addButton(toolMenu[tools[i]]->getToolBtn());
	}

}

void ToolLauncher::loadToolTips(bool connected){
	if (connected){
		ui->btnHome->setToolTip(QString("Click to open the home menu"));
		toolMenu["Digital IO"]->getToolBtn()->setToolTip(
					QString("Click to open the Digital IO tool"));
		toolMenu["Logic Analyzer"]->getToolBtn()->setToolTip(
					QString("Click to open the Logical Analyzer tool"));
		toolMenu["Network Analyzer"]->getToolBtn()->setToolTip(
					QString("Click to open the Network Analyzer tool"));
		toolMenu["Oscilloscope"]->getToolBtn()->setToolTip(
					QString("Click to open the Oscilloscope tool"));
		toolMenu["Pattern Generator"]->getToolBtn()->setToolTip(
					QString("Click to open the Pattern Generator tool"));
		toolMenu["Power Supply"]->getToolBtn()->setToolTip(
					QString("Click to open the Power Supply tool"));
		toolMenu["Signal Generator"]->getToolBtn()->setToolTip(
					QString("Click to open the Signal Generator tool"));
		toolMenu["Spectrum Analyzer"]->getToolBtn()->setToolTip(
					QString("Click to open the Spectrum Analyzer tool"));
		toolMenu["Voltmeter"]->getToolBtn()->setToolTip(
					QString("Click to open the Voltmeter tool"));
		ui->btnConnect->setToolTip(QString("Click to disconnect the device"));
	} else {
		ui->btnHome->setToolTip(QString());
		toolMenu["Digital IO"]->getToolBtn()->setToolTip(
					QString());
		toolMenu["Logic Analyzer"]->getToolBtn()->setToolTip(
					QString());
		toolMenu["Network Analyzer"]->getToolBtn()->setToolTip(
					QString());
		toolMenu["Oscilloscope"]->getToolBtn()->setToolTip(
					QString());
		toolMenu["Pattern Generator"]->getToolBtn()->setToolTip(
					QString());
		toolMenu["Power Supply"]->getToolBtn()->setToolTip(
					QString());
		toolMenu["Signal Generator"]->getToolBtn()->setToolTip(
					QString());
		toolMenu["Spectrum Analyzer"]->getToolBtn()->setToolTip(
					QString());
		toolMenu["Voltmeter"]->getToolBtn()->setToolTip(
					QString());
		ui->btnConnect->setToolTip(QString("Select a device first"));
	}
}

void ToolLauncher::update()
{
	updateListOfDevices(watcher.result());
}

ToolLauncher::~ToolLauncher()
{
	disconnect();

	for (auto it = devices.begin(); it != devices.end(); ++it) {
		delete *it;
	}

	devices.clear();

	delete search_timer;
	delete alive_timer;

	tl_api->ApiObject::save(*settings);
	delete settings;
	delete tl_api;
	delete ui;

	saveSettings();
}

void ToolLauncher::destroyPopup()
{
	auto *popup = static_cast<pv::widgets::Popup *>(QObject::sender());

	popup->deleteLater();
}

QPushButton *ToolLauncher::addContext(const QString& uri)
{
	auto pair = new QPair<QWidget, Ui::Device>;
	pair->second.setupUi(&pair->first);

	pair->second.description->setText(uri);

	ui->devicesList->addWidget(&pair->first);

	connect(pair->second.btn, SIGNAL(clicked(bool)),
	        this, SLOT(device_btn_clicked(bool)));

	pair->second.btn->setProperty("uri", QVariant(uri));
	devices.append(pair);

	return pair->second.btn;
}

void ToolLauncher::addRemoteContext()
{
	pv::widgets::Popup *popup = new pv::widgets::Popup(ui->homeWidget);
	connect(popup, SIGNAL(closed()), this, SLOT(destroyPopup()));

	QPoint pos = ui->groupBox->mapToGlobal(ui->btnAdd->pos());
	pos += QPoint(ui->btnAdd->width() / 2, ui->btnAdd->height());

	popup->set_position(pos, pv::widgets::Popup::Bottom);
	popup->show();

	ConnectDialog *dialog = new ConnectDialog(popup);
	connect(dialog, &ConnectDialog::newContext,
	[=](const QString& uri) {
		addContext(uri);
		popup->close();
	});
}

void ToolLauncher::swapMenu(QWidget *menu)
{
	if (current) {
		current->setVisible(false);
		ui->centralLayout->removeWidget(current);
	}

	current = menu;

	ui->centralLayout->addWidget(current);
	current->setVisible(true);
}

void ToolLauncher::setButtonBackground(bool on)
{

	auto *btn = static_cast<QPushButton *>(QObject::sender());

	setDynamicProperty(btn->parentWidget(), "selected", on);
}

void ToolLauncher::btnOscilloscope_clicked()
{
	swapMenu(static_cast<QWidget *>(oscilloscope));
}

void ToolLauncher::btnSignalGenerator_clicked()
{
	swapMenu(static_cast<QWidget *>(signal_generator));
}

void ToolLauncher::btnDMM_clicked()
{
	swapMenu(static_cast<QWidget *>(dmm));
}

void ToolLauncher::btnPowerControl_clicked()
{
	swapMenu(static_cast<QWidget *>(power_control));
}

void ToolLauncher::btnLogicAnalyzer_clicked()
{
	swapMenu(static_cast<QWidget *>(logic_analyzer));
}

void adiscope::ToolLauncher::btnPatternGenerator_clicked()
{
	swapMenu(static_cast<QWidget *>(pattern_generator));
}

void adiscope::ToolLauncher::btnNetworkAnalyzer_clicked()
{
	swapMenu(static_cast<QWidget *>(network_analyzer));
}

void adiscope::ToolLauncher::btnSpectrumAnalyzer_clicked()
{
	swapMenu(static_cast<QWidget *>(spectrum_analyzer));
}

void adiscope::ToolLauncher::btnDigitalIO_clicked()
{
	swapMenu(static_cast<QWidget *>(dio));
}

void adiscope::ToolLauncher::on_btnHome_clicked()
{
	swapMenu(ui->homeWidget);
}

void adiscope::ToolLauncher::resetStylesheets()
{
	setDynamicProperty(ui->btnConnect, "connected", false);
	setDynamicProperty(ui->btnConnect, "failed", false);

	for (auto it = devices.begin(); it != devices.end(); ++it) {
		QPushButton *btn = (*it)->second.btn;
		setDynamicProperty(btn, "connected", false);
		setDynamicProperty(btn, "failed", false);
	}
}

void adiscope::ToolLauncher::device_btn_clicked(bool pressed)
{
	if (pressed) {
		for (auto it = devices.begin(); it != devices.end(); ++it)
			if ((*it)->second.btn != sender()) {
				(*it)->second.btn->setChecked(false);
			}

		if (ui->btnConnect->property("connected").toBool()) {
			ui->btnConnect->click();
		}
	} else {
		disconnect();
	}

	resetStylesheets();
	ui->btnConnect->setEnabled(pressed);
	if (pressed){
		ui->btnConnect->setToolTip(QString("Click to connect the device"));
	} else {
		ui->btnConnect->setToolTip(QString("Select a device first"));
	}
}

void adiscope::ToolLauncher::disconnect()
{
	/* Switch back to home screen */
	ui->btnHome->click();

	if (ctx) {
		alive_timer->stop();
		toolMenu["Digital IO"]->getToolStopBtn()->setChecked(false);
		toolMenu["Logic Analyzer"]->getToolStopBtn()->setChecked(false);
		toolMenu["Network Analyzer"]->getToolStopBtn()->setChecked(false);
		toolMenu["Oscilloscope"]->getToolStopBtn()->setChecked(false);
		toolMenu["Pattern Generator"]->getToolStopBtn()->setChecked(false);
		toolMenu["Power Supply"]->getToolStopBtn()->setChecked(false);
		toolMenu["Signal Generator"]->getToolStopBtn()->setChecked(false);
		toolMenu["Spectrum Analyzer"]->getToolStopBtn()->setChecked(false);
		toolMenu["Voltmeter"]->getToolStopBtn()->setChecked(false);

		destroyContext();
		loadToolTips(false);
		resetStylesheets();
		search_timer->start(TIMER_TIMEOUT_MS);
	}

	/* Update the list of devices now */
	updateListOfDevices(searchDevices());
}

void adiscope::ToolLauncher::ping()
{
	int ret = iio_context_get_version(ctx, nullptr, nullptr, nullptr);

	if (ret < 0)
		disconnect();
}

void adiscope::ToolLauncher::on_btnConnect_clicked(bool pressed)
{
	if (ctx) {
		disconnect();
		ui->btnConnect->setToolTip(QString("Click to connect the device"));
		return;
	}

	QPushButton *btn = nullptr;
	QLabel *label = nullptr;

	for (auto it = devices.begin(); !btn && it != devices.end(); ++it) {
		if ((*it)->second.btn->isChecked()) {
			btn = (*it)->second.btn;
			label = (*it)->second.name;
		}
	}

	if (!btn) {
		throw std::runtime_error("No enabled device!");
	}

	QString uri = btn->property("uri").toString();

	bool success = switchContext(uri);
	if (success) {
		setDynamicProperty(ui->btnConnect, "connected", true);
		setDynamicProperty(btn, "connected", true);
		search_timer->stop();

		if (label) {
			label->setText(filter->hw_name());
		}
	} else {
		setDynamicProperty(ui->btnConnect, "failed", true);
		setDynamicProperty(btn, "failed", true);
	}

	Q_EMIT connectionDone(success);
}

void adiscope::ToolLauncher::destroyContext()
{
	if (dio) {
		delete dio;
		dio = nullptr;
	}

	if (dmm) {
		delete dmm;
		dmm = nullptr;
	}

	if (power_control) {
		delete power_control;
		power_control = nullptr;
	}

	if (signal_generator) {
		delete signal_generator;
		signal_generator = nullptr;
	}

	if (oscilloscope) {
		delete oscilloscope;
		oscilloscope = nullptr;
	}

	if (logic_analyzer) {
		delete logic_analyzer;
		logic_analyzer = nullptr;
	}

	if (pattern_generator) {
		delete pattern_generator;
		pattern_generator = nullptr;
	}

	if (network_analyzer) {
		delete network_analyzer;
		network_analyzer = nullptr;
	}

	if (spectrum_analyzer) {
		delete spectrum_analyzer;
		spectrum_analyzer = nullptr;
	}

	if (filter) {
		delete filter;
		filter = nullptr;
	}

	if (ctx) {
		iio_context_destroy(ctx);
		ctx = nullptr;
	}
}

bool ToolLauncher::loadDecoders(QString path)
{
	static bool srd_loaded = false;

	if (srd_loaded) {
		srd_exit();
	}

	if (srd_init(path.toStdString().c_str()) != SRD_OK) {
		qDebug() << "ERROR: libsigrokdecode init failed.";
		return false;
	} else {
		srd_loaded = true;
		/* Load the protocol decoders */
		srd_decoder_load_all();
		auto decoder = srd_decoder_get_by_id("parallel");

		if (decoder == nullptr) {
			return false;
		}
	}

	return true;
}

void adiscope::ToolLauncher::calibrate()
{
	auto old_dmm_text = toolMenu["Voltmeter"]->getToolBtn()->text();
	auto old_osc_text = toolMenu["Oscilloscope"]->getToolBtn()->text();
	auto old_siggen_text = toolMenu["Signal Generator"]->getToolBtn()->text();
	auto old_spectrum_text = toolMenu["Spectrum Analyzer"]->getToolBtn()->text();

	toolMenu["Voltmeter"]->getToolBtn()->setText("Calibrating...");
	toolMenu["Oscilloscope"]->getToolBtn()->setText("Calibrating...");
	toolMenu["Signal Generator"]->getToolBtn()->setText("Calibrating...");
	toolMenu["Spectrum Analyzer"]->getToolBtn()->setText("Calibrating...");

	Calibration calib(ctx);

	calib.initialize();
	calib.calibrateAll();
	calib.restoreTriggerSetup();

	auto m2k_adc = std::dynamic_pointer_cast<M2kAdc>(adc);
	if (m2k_adc) {
		m2k_adc->setChnCorrectionOffset(0, calib.adcOffsetChannel0());
		m2k_adc->setChnCorrectionOffset(1, calib.adcOffsetChannel1());
		m2k_adc->setChnCorrectionGain(0, calib.adcGainChannel0());
		m2k_adc->setChnCorrectionGain(1, calib.adcGainChannel1());
	}

	toolMenu["Voltmeter"]->getToolBtn()->setText(old_dmm_text);
	toolMenu["Oscilloscope"]->getToolBtn()->setText(old_osc_text);
	toolMenu["Signal Generator"]->getToolBtn()->setText(old_siggen_text);
	toolMenu["Spectrum Analyzer"]->getToolBtn()->setText(old_spectrum_text);

	Q_EMIT adcCalibrationDone();
	Q_EMIT dacCalibrationDone(calib.dacAvlsb(), calib.dacBvlsb());
}

void adiscope::ToolLauncher::enableAdcBasedTools()
{
	if (filter->compatible(TOOL_OSCILLOSCOPE)) {
		oscilloscope = new Oscilloscope(ctx, filter, adc,
						toolMenu["Oscilloscope"]->getToolStopBtn(),
						&js_engine, this);
		adc_users_group.addButton(toolMenu["Oscilloscope"]->getToolStopBtn());
	}

	if (filter->compatible(TOOL_DMM)) {
		dmm = new DMM(ctx, filter, adc, toolMenu["Voltmeter"]->getToolStopBtn(),
				&js_engine, this);
		adc_users_group.addButton(toolMenu["Voltmeter"]->getToolStopBtn());
	}

	if (filter->compatible(TOOL_SPECTRUM_ANALYZER)) {
		spectrum_analyzer = new SpectrumAnalyzer(ctx, filter, adc,
			toolMenu["Spectrum Analyzer"]->getToolStopBtn(), this);
		adc_users_group.addButton(toolMenu["Spectrum Analyzer"]->getToolStopBtn());
	}

	Q_EMIT adcToolsCreated();
}

void adiscope::ToolLauncher::enableDacBasedTools(float dacA_vlsb,
                float dacB_vlsb)
{
	if (filter->compatible(TOOL_SIGNAL_GENERATOR)) {
		signal_generator = new SignalGenerator(ctx, filter,
							toolMenu["Signal Generator"]->getToolStopBtn(), &js_engine, this);

		struct iio_device *dev = iio_context_find_device(ctx, "m2k-dac-a");

		if (dev) {
			struct iio_channel *chn = iio_device_find_channel(dev,
							"voltage0", true);

			if (chn)
				signal_generator->set_vlsb_of_channel(
					iio_channel_get_id(chn),
					iio_device_get_name(dev), dacA_vlsb);
		}

		dev = iio_context_find_device(ctx, "m2k-dac-b");

		if (dev) {
			struct iio_channel *chn = iio_device_find_channel(dev,
						"voltage0", true);

			if (chn)
				signal_generator->set_vlsb_of_channel(
					iio_channel_get_id(chn),
					iio_device_get_name(dev), dacB_vlsb);
		}
	}

	Q_EMIT dacToolsCreated();
}

bool adiscope::ToolLauncher::switchContext(const QString& uri)
{
	destroyContext();

	if (uri.startsWith("ip:")) {
		previousIp = uri.mid(3);
	}

	ctx = iio_create_context_from_uri(uri.toStdString().c_str());

	if (!ctx) {
		return false;
	}

	alive_timer->start(ALIVE_TIMER_TIMEOUT_MS);

	filter = new Filter(ctx);

	if (filter->hw_name().compare("M2K") == 0) {
		adc = AdcBuilder::newAdc(AdcBuilder::M2K, ctx,
			filter->find_device(ctx, TOOL_OSCILLOSCOPE));
	} else {
		adc = AdcBuilder::newAdc(AdcBuilder::GENERIC, ctx,
			filter->find_device(ctx, TOOL_OSCILLOSCOPE));
	}

	if (filter->compatible(TOOL_PATTERN_GENERATOR)
	    || filter->compatible(TOOL_DIGITALIO)) {
		dioManager = new DIOManager(ctx,filter);

	}

	if (filter->compatible(TOOL_LOGIC_ANALYZER)
	    || filter->compatible(TOOL_PATTERN_GENERATOR)) {
		bool success = loadDecoders(QCoreApplication::applicationDirPath() +
					"/decoders");

		if (!success) {
			search_timer->stop();

			QMessageBox error(this);
			error.setText("There was a problem initializing libsigrokdecode. Some features may be missing");
			error.exec();
		}
	}

	if (filter->compatible(TOOL_DIGITALIO)) {
		dio = new DigitalIO(ctx, filter, toolMenu["Digital IO"]->getToolStopBtn(),
				dioManager, &js_engine, this);
	}


	if (filter->compatible(TOOL_POWER_CONTROLLER)) {
		power_control = new PowerController(ctx, toolMenu["Power Supply"]->getToolStopBtn(),
				&js_engine, this);
	}

	if (filter->compatible(TOOL_LOGIC_ANALYZER)) {
		logic_analyzer = new LogicAnalyzer(ctx, filter, toolMenu["Logic Analyzer"]->getToolStopBtn(),
				&js_engine, this);
	}


	if (filter->compatible((TOOL_PATTERN_GENERATOR))) {
		pattern_generator = new PatternGenerator(ctx, filter,
				toolMenu["Pattern Generator"]->getToolStopBtn(), &js_engine,dioManager, this);
	}


	if (filter->compatible((TOOL_NETWORK_ANALYZER))) {
		network_analyzer = new NetworkAnalyzer(ctx, filter,
						       toolMenu["Network Analyzer"]->getToolStopBtn(), &js_engine, this);
	}

	loadToolTips(true);
	QtConcurrent::run(std::bind(&ToolLauncher::calibrate, this));

	return true;
}

void ToolLauncher::hasText()
{
	QTextStream in(stdin);
	QTextStream out(stdout);

	js_cmd.append(in.readLine());

	unsigned int nb_open_braces = js_cmd.count(QChar('{'));
	unsigned int nb_closing_braces = js_cmd.count(QChar('}'));

	if (nb_open_braces == nb_closing_braces) {
		QJSValue val = js_engine.evaluate(js_cmd);

		if (val.isError()) {
			out << "Exception:" << val.toString() << endl;
		} else if (!val.isUndefined()) {
			out << val.toString() << endl;
		}

		js_cmd.clear();
		out << "scopy > ";
	} else {
		js_cmd.append(QChar('\n'));

		out << "> ";
	}

	out.flush();
}

void ToolLauncher::checkIp(const QString& ip)
{
	if (iio_create_network_context(ip.toStdString().c_str())) {
		previousIp = ip;

		QString uri = "ip:" + ip;
		QMetaObject::invokeMethod(this, "addContext",
					Qt::QueuedConnection,
					Q_ARG(const QString&, uri));
	} else {
		previousIp = "";
	}
}

void ToolLauncher::toolDetached(bool detached)
{
	Tool *tool = static_cast<Tool *>(QObject::sender());

	if (detached) {
		/* Switch back to the home screen */
		if (current == static_cast<QWidget *>(tool))
			ui->btnHome->click();

		DetachedWindow *window = new DetachedWindow(this);
		window->setCentralWidget(tool);
		window->show();

		connect(window, SIGNAL(closed()), tool, SLOT(attached()));
	}

	tool->setVisible(detached);
	tool->runButton()->parentWidget()->setEnabled(!detached);
}

void ToolLauncher::swapMenuOptions(int source, int destination, bool dropAfter)
{
	QWidget *sourceWidget = ui->menuOptionsLayout->itemAt(source)->widget();
	if (dropAfter == true){
		for (int i = source + 1; i < destination + 1; i++ ){
			QWidget *dest = ui->menuOptionsLayout->itemAt(i)->widget();
			UpdatePosition(dest, i - 1);
		}
		UpdatePosition(sourceWidget, destination);
		ui->menuOptionsLayout->removeWidget(sourceWidget);
		ui->menuOptionsLayout->insertWidget(destination, sourceWidget);
		return;
	}
	if (destination == MAX_MENU_OPTIONS - 1 && source != MAX_MENU_OPTIONS - 2){
		for (int i = source + 1; i < MAX_MENU_OPTIONS; i++ ){
			QWidget *dest = ui->menuOptionsLayout->itemAt(i)->widget();
			UpdatePosition(dest, i - 1);
		}
		UpdatePosition(sourceWidget, destination);
		ui->menuOptionsLayout->removeWidget(sourceWidget);
		ui->menuOptionsLayout->insertWidget(destination, sourceWidget);
		return;
	}
	if (destination == 0){
		for (int i = 0; i < source; i++ ){
			QWidget *dest = ui->menuOptionsLayout->itemAt(i)->widget();
			UpdatePosition(dest, i + 1);
		}
		UpdatePosition(sourceWidget, destination);
		ui->menuOptionsLayout->removeWidget(sourceWidget);
		ui->menuOptionsLayout->insertWidget(destination, sourceWidget);
		return;
	}
	if (source < destination){
		for (int i = source + 1; i < destination; i++ ){
			QWidget *dest = ui->menuOptionsLayout->itemAt(i)->widget();
			UpdatePosition(dest, i - 1);
		}
		UpdatePosition(sourceWidget, destination - 1);
		ui->menuOptionsLayout->removeWidget(sourceWidget);
		ui->menuOptionsLayout->insertWidget(destination - 1, sourceWidget);
		return;
	} else {
		for (int i = destination; i < source; i++ ){
			QWidget *dest = ui->menuOptionsLayout->itemAt(i)->widget();
			UpdatePosition(dest, i + 1);
		}
	}
	UpdatePosition(sourceWidget, destination);
	ui->menuOptionsLayout->removeWidget(sourceWidget);
	ui->menuOptionsLayout->insertWidget(destination, sourceWidget);
}

void ToolLauncher::UpdatePosition(QWidget *widget, int position){
	MenuOption *menuOption = static_cast<MenuOption *>(widget);
	menuOption->setPosition(position);
}

bool ToolLauncher_API::menu_opened() const
{
	return tl->ui->btnMenu->isChecked();
}

void ToolLauncher_API::open_menu(bool open)
{
	tl->ui->btnMenu->setChecked(open);
}

bool ToolLauncher_API::hidden() const
{
	return !tl->isVisible();
}

void ToolLauncher_API::hide(bool hide)
{
	tl->setVisible(!hide);
}

bool ToolLauncher_API::connect(const QString& uri)
{
	QPushButton *btn = nullptr;
	bool did_connect = false;
	bool done = false;

	for (auto it = tl->devices.begin();
	     !btn && it != tl->devices.end(); ++it) {
		QPushButton *tmp = (*it)->second.btn;

		if (tmp->property("uri").toString().compare(uri) == 0) {
			btn = tmp;
		}
	}

	if (!btn) {
		btn = tl->addContext(uri);
	}

	tl->connect(tl, &ToolLauncher::connectionDone,
			[&](bool success) {
		if (!success)
			done = true;
	});

	tl->connect(tl, &ToolLauncher::adcToolsCreated, [&]() {
		did_connect = true;
		done = true;
	});

	btn->click();
	tl->ui->btnConnect->click();

	do {
		QCoreApplication::processEvents();
		QThread::msleep(10);
	} while (!done);
	return did_connect;
}

void ToolLauncher_API::disconnect()
{
	tl->disconnect();
}

void ToolLauncher_API::addIp(const QString& ip)
{
	if (!ip.isEmpty()) {
		QtConcurrent::run(std::bind(&ToolLauncher::checkIp, tl, ip));
	}
}

void ToolLauncher_API::load(const QString& file)
{
	QSettings settings(file, QSettings::IniFormat);

	this->ApiObject::load(settings);

	if (tl->oscilloscope)
		tl->oscilloscope->api->load(settings);
	if (tl->dmm)
		tl->dmm->api->load(settings);
	if (tl->power_control)
		tl->power_control->api->load(settings);
	if (tl->signal_generator)
		tl->signal_generator->api->load(settings);
	if (tl->logic_analyzer)
		tl->logic_analyzer->api->load(settings);
	if (tl->dio)
		tl->dio->api->load(settings);
	if (tl->pattern_generator)
		tl->pattern_generator->api->load(settings);
	if (tl->network_analyzer)
		tl->network_analyzer->api->load(settings);
	if (tl->spectrum_analyzer)
		tl->spectrum_analyzer->api->load(settings);
}

void ToolLauncher_API::save(const QString& file)
{
	QSettings settings(file, QSettings::IniFormat);

	this->ApiObject::save(settings);

	if (tl->oscilloscope)
		tl->oscilloscope->api->save(settings);
	if (tl->dmm)
		tl->dmm->api->save(settings);
	if (tl->power_control)
		tl->power_control->api->save(settings);
	if (tl->signal_generator)
		tl->signal_generator->api->save(settings);
	if (tl->logic_analyzer)
		tl->logic_analyzer->api->save(settings);
	if (tl->dio)
		tl->dio->api->save(settings);
	if (tl->pattern_generator)
		tl->pattern_generator->api->save(settings);
	if (tl->network_analyzer)
		tl->network_analyzer->api->save(settings);
	if (tl->spectrum_analyzer)
		tl->spectrum_analyzer->api->save(settings);
}
