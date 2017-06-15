/*
 * Copyright 2017 Analog Devices, Inc.
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

#ifndef SCOPY_TOOL_HPP
#define SCOPY_TOOL_HPP

#include <QWidget>
#include <QSettings>

class QJSEngine;
class QPushButton;

extern "C" {
	struct iio_context;
}

namespace adiscope {
class ApiObject;
class ToolLauncher;

class Tool : public QWidget
{
	Q_OBJECT

public:
	explicit Tool(struct iio_context *ctx, QPushButton *runButton,
			ApiObject *api, ToolLauncher *parent);
	~Tool();

	QPushButton *runButton() { return this->run_button; }

Q_SIGNALS:
	void detachedState(bool detached);

public Q_SLOTS:
	virtual void attached();

protected:
	struct iio_context *ctx;
	QPushButton *run_button;
	ApiObject *api;
	QSettings *settings;
};
}

#endif /* SCOPY_TOOL_HPP */
