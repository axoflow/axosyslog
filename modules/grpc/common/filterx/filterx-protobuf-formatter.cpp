/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 shifter
 * Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "filterx-protobuf-formatter.hpp"

#include "compat/cpp-start.h"
#include "filterx/filterx-mapping.h"
#include "filterx/object-extractor.h"
#include "compat/cpp-end.h"

#include "protobuf-field-converter.hpp"

using namespace syslogng::grpc;

FilterXProtobufFormatter::FilterXProtobufFormatter(const std::string &proto_file_path)
{
  this->proto_schema_file_loader.set_proto_file_path(proto_file_path);
  if (!this->proto_schema_file_loader.init())
    throw std::runtime_error("Failed to initialize protobuf schema loader");
}

static gboolean
_format_element(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  try
    {
      google::protobuf::Message *message = static_cast<google::protobuf::Message *>(user_data);

      std::string field_name = extract_string_from_object(key);
      ProtoReflectors reflectors(*message, field_name);
      ProtobufFieldConverter *converter = get_protobuf_field_converter(reflectors.field_type);

      FilterXObject *assoc_object = NULL;
      if (reflectors.field_descriptor->is_repeated())
        {
          if (!converter->set_repeated(message, field_name, value, &assoc_object))
            return FALSE;
        }
      else
        {
          if (!converter->set(message, field_name, value, &assoc_object))
            return FALSE;
        }

      filterx_object_unref(assoc_object);
    }
  catch (const std::exception &e)
    {
      filterx_eval_push_error_info_printf("Failed to format element", NULL,
                                          "key type: %s, value type: %s, error: %s",
                                          filterx_object_get_type_name(key),
                                          filterx_object_get_type_name(value),
                                          e.what());
      return FALSE;
    }

  return TRUE;
}

google::protobuf::Message *
FilterXProtobufFormatter::format(FilterXObject *object) const
{
  FilterXObject *object_unwrapped = filterx_ref_unwrap_ro(object);
  if (!filterx_object_is_type(object, &FILTERX_TYPE_NAME(mapping)))
    {
      throw std::runtime_error("Expected a dictionary object for protobuf formatting");
    }

  google::protobuf::Message *message = this->proto_schema_file_loader.get_schema_prototype().New();

  gpointer user_data = static_cast<gpointer>(message);
  gboolean success = filterx_object_iter(object_unwrapped, _format_element, user_data);

  if (!success)
    {
      delete message;
      throw std::runtime_error("Failed to format dict to protobuf message");
    }

  return message;
}
