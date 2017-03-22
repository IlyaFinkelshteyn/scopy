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

#ifndef APIOBJECT_HPP
#define APIOBJECT_HPP

#include <QObject>

class QJSEngine;
class QSettings;
template <typename T> class QList;

namespace adiscope {
	class ApiObject : public QObject
	{
		Q_OBJECT

	public:
		ApiObject();
		~ApiObject();

		void load();
		void load(QSettings&);
		void save();
		void save(QSettings&);

		void js_register(QJSEngine *engine);

	private:
		template <typename T> void save(QSettings& settings,
				const QString& prop, const QList<T>& list);
		template <typename T> QList<T> load(QSettings& settings,
				const QString& prop);

		void save_obj(QSettings& settings, const QString& prop,
				const QList<ApiObject*> list);
		void load_obj(QSettings& settings, const QString& prop,
				const QList<ApiObject*> list);

		void _save(ApiObject *, QSettings&);
		void _load(ApiObject *, QSettings&);

		bool obj_list_from_qvariant(const QVariant& data,
			QList<ApiObject*>& objects);
	};
}

#endif /* APIOBJECT_HPP */
