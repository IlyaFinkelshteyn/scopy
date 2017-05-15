#ifndef CONNECTMENU_H
#define CONNECTMENU_H

#include <QWidget>

extern "C"{
struct iio_context;
}

namespace Ui {
class ConnectMenu;
}

namespace adiscope{
class ConnectMenu : public QWidget
{
	Q_OBJECT

public:
	explicit ConnectMenu(QWidget *parent = 0);
	~ConnectMenu();
	void focus();

Q_SIGNALS:
	void abort();
	void newContext(const QString& uri);
	void finished(struct iio_context *ctx);


private Q_SLOTS:
	void on_abortBtn_clicked();
	void connectBtnClicked();
	void discardSettings();
	void DeviceFound(struct iio_context *ctx);
	void AddDevice();

private:
	Ui::ConnectMenu *ui;
	bool connected;
	void createContext(const QString& uri);
};
}
#endif // CONNECTMENU_H
