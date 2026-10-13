#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################
from pathlib import Path


def test_list_paths_prints_tracked_files_without_initializing(config, syslog_ng, port_allocator, tmp_path):
    config.add_include("scl.conf")
    credentials = tmp_path / "service-account.json"
    credentials.write_text("{}")
    port = port_allocator()
    pubsub_destination = config.create_http_destination(
        port=port,
        driver_name="google_pubsub",
        service_endpoint=config.stringify("http://127.0.0.1:{}".format(port)),
        project=config.stringify("example-project"),
        topic=config.stringify("example-topic"),
        credentials=config.stringify(str(credentials)),
    )
    config.create_logpath(statements=[config.create_example_msg_generator_source(num=1), pubsub_destination])

    output = syslog_ng.list_paths(config)

    assert output == "path_secret: {}\n".format(credentials)
    assert not Path(syslog_ng.instance_paths.get_control_socket_path()).exists()
    assert not Path(syslog_ng.instance_paths.get_persist_path()).exists()


def test_list_paths_without_tracked_files(config, syslog_ng):
    config.create_logpath(statements=[config.create_example_msg_generator_source(num=1)])

    assert syslog_ng.list_paths(config) == "No files available\n"


def test_list_paths_reports_missing_files(config, syslog_ng, port_allocator, tmp_path):
    config.add_include("scl.conf")
    credentials = tmp_path / "missing-service-account.json"
    port = port_allocator()
    pubsub_destination = config.create_http_destination(
        port=port,
        driver_name="google_pubsub",
        service_endpoint=config.stringify("http://127.0.0.1:{}".format(port)),
        project=config.stringify("example-project"),
        topic=config.stringify("example-topic"),
        credentials=config.stringify(str(credentials)),
    )
    config.create_logpath(statements=[config.create_example_msg_generator_source(num=1), pubsub_destination])

    assert syslog_ng.list_paths(config) == "path_secret: {}\n".format(credentials)


def _list_paths_without_errors(syslog_ng, config):
    # With the debug output off, a file that was read and found missing is the only thing on stderr.
    syslog_ng.start_params.debug = syslog_ng.start_params.trace = False
    syslog_ng.start_params.verbose = syslog_ng.start_params.startup_debug = False

    output = syslog_ng.list_paths(config)

    assert syslog_ng.instance_paths.get_list_paths_stderr_path().read_text() == ""
    return output


def test_list_paths_lists_a_missing_in_list_file(config, syslog_ng, tmp_path):
    missing_list_file = tmp_path / "missing.list"
    config.set_raw_config(
        f"""
@version: {config.get_version()}

filter f_list {{ in-list("{missing_list_file}", value("PROGRAM")); }};

log {{
    source {{ example-msg-generator(num(1)); }};
    filter(f_list);
}};
""",
    )

    assert _list_paths_without_errors(syslog_ng, config) == "path_no_check: {}\n".format(missing_list_file)


def test_list_paths_lists_missing_grpc_credential_files(config, syslog_ng, port_allocator, tmp_path):
    config.set_raw_config(
        f"""
@version: {config.get_version()}

log {{
    source {{
        opentelemetry(port({port_allocator()})
            auth(tls(key-file("{tmp_path}/server.key") cert-file("{tmp_path}/server.crt") ca-file("{tmp_path}/server-ca.crt"))));
    }};
    destination {{
        opentelemetry(url("127.0.0.1:{port_allocator()}")
            auth(tls(key-file("{tmp_path}/client.key") cert-file("{tmp_path}/client.crt") ca-file("{tmp_path}/client-ca.crt"))));
    }};
    destination {{
        opentelemetry(url("127.0.0.1:{port_allocator()}") auth(service-account(key("{tmp_path}/service-account.json"))));
    }};
}};
""",
    )

    assert _list_paths_without_errors(syslog_ng, config) == (
        f"path_secret: {tmp_path}/server.key\n"
        f"path_no_check: {tmp_path}/server.crt\n"
        f"path_no_check: {tmp_path}/server-ca.crt\n"
        f"path_secret: {tmp_path}/client.key\n"
        f"path_no_check: {tmp_path}/client.crt\n"
        f"path_no_check: {tmp_path}/client-ca.crt\n"
        f"path_secret: {tmp_path}/service-account.json\n"
    )


def test_list_paths_lists_directories_but_syslog_ng_ctl_list_files_does_not(config, syslog_ng, syslog_ng_ctl, port_allocator, tmp_path):
    list_file = tmp_path / "programs.list"
    list_file.write_text("example\n")
    disk_buffer_dir = tmp_path / "disk-buffer"
    config.set_raw_config(
        f"""
@version: {config.get_version()}

filter f_list {{ in-list("{list_file}", value("PROGRAM")); }};

destination d_network {{
    network("127.0.0.1" port({port_allocator()}) disk-buffer(dir("{disk_buffer_dir}") reliable(no)));
}};

log {{
    source {{ example-msg-generator(num(1)); }};
    filter(f_list);
    destination(d_network);
}};
""",
    )

    assert syslog_ng.list_paths(config) == "path_no_check: {}\npath_no_check: {}\n".format(list_file, disk_buffer_dir)

    syslog_ng.start(config)
    assert syslog_ng_ctl.list_files()["stdout"] == "path_no_check: {}\n".format(list_file)


def test_tracked_paths_are_not_checked_where_they_were_plain_strings(config, syslog_ng, port_allocator, tmp_path):
    missing_ca_dir = tmp_path / "missing-ca.d"
    config.set_raw_config(
        f"""
@version: {config.get_version()}

destination d_http {{
    http(url("https://127.0.0.1:{port_allocator()}/") tls(ca-dir("{missing_ca_dir}")));
}};

log {{
    source {{ example-msg-generator(num(1)); }};
    destination(d_http);
}};
""",
    )

    assert syslog_ng.list_paths(config) == "path_no_check: {}\n".format(missing_ca_dir)

    syslog_ng.start(config)
