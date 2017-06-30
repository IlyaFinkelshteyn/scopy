#ifndef MENUOPTION_H
#define MENUOPTION_H

#include <QWidget>
#include <QMouseEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QPushButton>
#include <QFrame>

#include "customPushButton.hpp"

namespace Ui {
class MenuOption;
}
namespace adiscope {
class MenuOption : public QWidget
{
	Q_OBJECT

public:
	explicit MenuOption(QString toolName, QString iconPath,
			    int position, bool usesCustomBtn,
			    QWidget *parent = 0);
	~MenuOption();

	QPushButton* getToolBtn();
	QPushButton* getToolStopBtn();
	void setPosition(int position);
	void paintEvent(QPaintEvent *pe);
private:

	int position;
	QString toolName;

	QPoint dragStartPosition;

	QRect topDragbox;
	QRect centerDragbox;
	QRect botDragbox;

	QFrame *topSep, *botSep;


	Ui::MenuOption *ui;
	QString iconPath;
	bool usesCustomBtn;
	void disableSeparatorsHighlight();
	void highlightTopSeparator();
	void highlightBotSeparator();

Q_SIGNALS:
	void requestPositionChange(int, int, bool);
private Q_SLOTS:
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event);
	void dragEnterEvent(QDragEnterEvent *event);
	void dragMoveEvent(QDragMoveEvent *event);
	void dropEvent(QDropEvent *event);
};
}

#endif // MENUOPTION_H
