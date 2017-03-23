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

#include "apiObject.hpp"

#include <QDebug>
#include <QJSEngine>
#include <QList>
#include <QMetaProperty>
#include <QSettings>

using namespace adiscope;

ApiObject::ApiObject() : QObject(nullptr)
{
}

ApiObject::~ApiObject()
{
}

template <typename T> void ApiObject::save(QSettings& settings,
		const QString& prop, const QList<T>& list)
{
	settings.beginWriteArray(prop, list.size());

	for (unsigned int i = 0; i < list.size(); i++) {
		settings.setArrayIndex(i);
		settings.setValue("idx", QVariant(list.at(i)));
	}

	settings.endArray();
}

template <typename T> QList<T> ApiObject::load(
		QSettings& settings, const QString& prop)
{
	int nb = settings.beginReadArray(prop);
	QList<T> list;

	for (unsigned int i = 0; i < nb; i++) {
		settings.setArrayIndex(i);
		list.append(settings.value("idx").value<T>());
	}

	settings.endArray();
	return list;
}

void ApiObject::save_obj(QSettings& settings, const QString& prop,
	const QList<ApiObject*> list)
{
	settings.beginWriteArray(prop, list.size());

	for (unsigned int i = 0; i < list.size(); i++) {
		settings.setArrayIndex(i);
		list.at(i)->save(settings);
	}

	settings.endArray();
}

void ApiObject::load_obj(QSettings& settings, const QString& prop,
	const QList<ApiObject*> list)
{
	int nb = settings.beginReadArray(prop);

	for (unsigned int i = 0; i < nb; i++) {
		settings.setArrayIndex(i);
		list.at(i)->load(settings);
	}

	settings.endArray();
}

void ApiObject::_load(ApiObject *obj, QSettings& settings)
{
	settings.beginGroup(obj->objectName());

	auto meta = obj->metaObject();
	for (unsigned int i = meta->propertyOffset();
			i < meta->propertyCount(); i++) {
		auto prop = meta->property(i);
		if (!prop.isStored() || !prop.isReadable())
			continue;

		int type = prop.userType();
		if (type >= QMetaType::User) {
			auto data = prop.read(obj);
			if (data.canConvert<QVariantList>()) {
				QList<ApiObject*> objs;
				bool is_api_obj_list = obj_list_from_qvariant(
					data, objs);
				if (is_api_obj_list) {
					load_obj(settings, prop.name(), objs);
				}
			} else if (data.canConvert(type)) {
				ApiObject *obj = data.value<ApiObject*>();
				obj->load(settings);
			}
		}

		if (!prop.isWritable())
			continue;

		auto data = prop.read(obj);

		if (data.canConvert<QList<bool>>()) {
			auto list = load<bool>(settings, prop.name());
			if (!list.empty())
				prop.write(this, QVariant::fromValue(list));
		} else if (data.canConvert<QList<int>>()) {
			auto list = load<int>(settings, prop.name());
			if (!list.empty())
				prop.write(this, QVariant::fromValue(list));
		} else if (data.canConvert<QList<double>>()) {
			auto list = load<double>(settings, prop.name());
			if (!list.empty())
				prop.write(this, QVariant::fromValue(list));
		} else if (data.canConvert<QList<QString>>()) {
			auto list = load<QString>(settings, prop.name());
			if (!list.empty())
				prop.write(this, QVariant::fromValue(list));
		} else {
			auto value = settings.value(prop.name());

			qDebug() << "Loading property"
				<< prop.name()
				<< "value" << value;

			if (!value.isNull())
				prop.write(this, value);
		}
	}

	settings.endGroup();
}

void ApiObject::_save(ApiObject *obj, QSettings& settings)
{
	settings.beginGroup(obj->objectName());

	auto meta = obj->metaObject();
	for (unsigned int i = meta->propertyOffset();
			i < meta->propertyCount(); i++) {
		auto prop = meta->property(i);

		if (!prop.isStored() || !prop.isReadable())
			continue;

		int type = prop.userType();
		if (type >= QMetaType::User) {
			auto data = prop.read(obj);
			if (data.canConvert<QVariantList>()) {
				QList<ApiObject*> objs;
				bool is_api_obj_list = obj_list_from_qvariant(
					data, objs);
				if (is_api_obj_list) {
					save_obj(settings, prop.name(), objs);
				}
			} else if (data.canConvert(type)) {
				ApiObject *obj = data.value<ApiObject*>();
				obj->save(settings);
			}
		}

		if (!prop.isWritable())
			continue;

		auto data = prop.read(obj);

		if (data.canConvert<QList<bool>>()) {
			save<bool>(settings, prop.name(),
					data.value<QList<bool>>());
		} else if (data.canConvert<QList<int>>()) {
			save<int>(settings, prop.name(),
					data.value<QList<int>>());
		} else if (data.canConvert<QList<double>>()) {
			save<double>(settings, prop.name(),
					data.value<QList<double>>());
		} else if (data.canConvert<QList<QString>>()) {
			save<QString>(settings, prop.name(),
					data.value<QList<QString>>());
		} else {
			qDebug() << "Saving property"
				<< prop.name()
				<< "value" << data;

			settings.setValue(prop.name(), data);
		}
	}

	settings.endGroup();
}

void ApiObject::load()
{
	QSettings settings;

	_load(this, settings);
}

void ApiObject::load(QSettings& settings)
{
	_load(this, settings);
}

void ApiObject::save()
{
	QSettings settings;

	_save(this, settings);
}

void ApiObject::save(QSettings& settings)
{
	_save(this, settings);
}

void ApiObject::js_register(QJSEngine *engine)
{
	if (engine) {
		engine->globalObject().setProperty(objectName(),
				engine->newQObject(this));
	}
}

bool ApiObject::obj_list_from_qvariant(const QVariant& data,
	QList<ApiObject*>& objects)
{
	bool is_api_obj_list = true;
	QSequentialIterable iterable = data.value<QSequentialIterable>();
	Q_FOREACH (const QVariant &v, iterable) {
		if (v.canConvert<ApiObject*>()) {
			ApiObject *obj = v.value<ApiObject*>();
			objects.append(obj);
		} else {
			is_api_obj_list = false;
			break;
		}
	}

	return is_api_obj_list;
}
