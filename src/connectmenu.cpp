#include "connectmenu.h"
#include "ui_connectmenu.h"
#include "dynamicWidget.hpp"
#include <QtConcurrentRun>
#include <QFocusEvent>
#include <functional>

#include <iio.h>

using namespace adiscope;

ConnectMenu::ConnectMenu(QWidget *parent) :
	QWidget(parent),
	ui(new Ui::ConnectMenu)
{
	ui->setupUi(this);
	connect(ui->connectBtn,SIGNAL(clicked()),this,SLOT(connectBtnClicked()));
	connect(ui->hostname,SIGNAL(returnPressed()),this,SLOT(connectBtnClicked()));
	ui->AddBtn->setDisabled(true);
	ui->connectBtn->setDisabled(true);
	connect(ui->hostname,SIGNAL(textChanged(const QString&)),this,
		SLOT(discardSettings()));
	connect(this,SIGNAL(finished(struct iio_context *)),this,
		SLOT(DeviceFound(struct iio_context *)));
	connect(ui->AddBtn,SIGNAL(clicked()),this,SLOT(AddDevice()));
}

void ConnectMenu::AddDevice()
{
	QString new_uri = "ip:" + ui->hostname->text();
	Q_EMIT newContext(new_uri);
	ui->AddBtn->setDisabled(true);
	ui->hostname->setText("");
	ui->hostname->setDisabled(false);
	focus();
}

ConnectMenu::~ConnectMenu()
{
	delete ui;
}

void ConnectMenu::discardSettings()
{
	if (!ui->hostname->text().isEmpty()) {
		ui->connectBtn->setDisabled(false);
	} else {
		ui->connectBtn->setDisabled(true);
	}

	setDynamicProperty(ui->connectBtn,"failed",false);
	setDynamicProperty(ui->AddBtn,"connected",false);
	setDynamicProperty(ui->connectBtn,"connected",false);
	ui->description->setPlainText(QString(""));
	this->connected = false;
	ui->status_label->setText(QString(""));


}

void ConnectMenu::createContext(const QString& uri)
{
	ui->hostname->setDisabled(true);
	struct iio_context *ctx_from_uri = iio_create_context_from_uri(
			uri.toStdString().c_str());
	ui->hostname->setDisabled(false);
	Q_EMIT finished(ctx_from_uri);
}

void ConnectMenu::DeviceFound(iio_context *ctx)
{
	this->connected = !!ctx;

	if (!!ctx) {
		QString description(iio_context_get_description(ctx));
		ui->description->setPlainText(description);
		setDynamicProperty(ui->connectBtn,"connected",true);
		setDynamicProperty(ui->AddBtn,"connected",true);
		ui->AddBtn->setDisabled(false);
		ui->hostname->setDisabled(true);
		ui->status_label->setText(QString("Succes: Found device!"));
		iio_context_destroy(ctx);
	} else {
		setDynamicProperty(ui->connectBtn,"failed",true);
		ui->status_label->setText(
			QString("Error: Unable to find host: No such host is known!"));
		focus();
	}
}

void ConnectMenu::connectBtnClicked()
{
	if (!ui->connectBtn->isEnabled()) {
		return;
	}

	ui->status_label->setText(QString("Waiting for connection..."));

	if (!connected) {
		ui->connectBtn->setDisabled(true);
		QString new_uri = "ip:" + ui->hostname->text();
		QtConcurrent::run(std::bind(&ConnectMenu::createContext,this,new_uri));
	}
}


void ConnectMenu::on_abortBtn_clicked()
{
	//abort signal so tool_laucnher will know to swap to the
	//"hello scopy page" in the stacked widget
	if (ui->AddBtn->isEnabled()) {
		ui->AddBtn->setDisabled(true);
		ui->hostname->setText("");
		ui->hostname->setDisabled(false);
	}

	Q_EMIT abort();
}

void ConnectMenu::focus()
{
	ui->hostname->activateWindow();
	ui->hostname->setFocus();
	//QFocusEvent* eventFocus = new QFocusEvent(QEvent::FocusIn);
	//qApp->postEvent(ui->hostname,(QEvent *)eventFocus,Qt::LowEventPriority);
	//ui->hostname->grabKeyboard();
}
