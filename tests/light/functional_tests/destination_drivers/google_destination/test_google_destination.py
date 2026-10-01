#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Balint Ferencz <balint.ferencz@axoflow.com>
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
import base64
import json
from pathlib import Path

import pytest
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa


CLIENT_EMAIL = "logger@example-project.iam.gserviceaccount.com"
PUBSUB_AUDIENCE = "https://pubsub.googleapis.com/google.pubsub.v1.Publisher"
CLOUD_PLATFORM_SCOPE = "https://www.googleapis.com/auth/cloud-platform"


@pytest.fixture
def service_account_key(tmp_path):
    private_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    pem = private_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    ).decode()
    path = tmp_path / "service-account.json"
    path.write_text(
        json.dumps({
            "type": "service_account",
            "project_id": "example-project",
            "private_key_id": "key-id",
            "private_key": pem,
            "client_email": CLIENT_EMAIL,
            "token_uri": "https://oauth2.googleapis.com/token",
        }),
    )
    return str(path)


def jwt_claims(headers):
    scheme, token = headers["Authorization"].split(" ", 1)
    assert scheme == "Bearer"
    payload = token.split(".")[1]
    return json.loads(base64.urlsafe_b64decode(payload + "=" * (-len(payload) % 4)))


def create_google_destination(config, port_allocator, driver_name, **options):
    config.add_include("scl.conf")
    port = port_allocator()
    return config.create_http_destination(
        port=port,
        driver_name=driver_name,
        service_endpoint=config.stringify("http://127.0.0.1:{}".format(port)),
        **options,
    )


@pytest.mark.parametrize(
    "auth_options", [
        pytest.param(lambda key: {"auth": {"service-account": {"key": '"{}"'.format(key)}}}, id="auth"),
        pytest.param(lambda key: {"credentials": '"{}"'.format(key)}, id="credentials"),
        pytest.param(
            lambda key: {"credentials": '"{}"'.format(key), "gcp_auth_header_params": {"ca_file": '"/nonexistent/ca.crt"', "timeout": 10}},
            id="credentials-with-gcp-auth-header-params",
        ),
    ],
)
def test_google_pubsub_signs_the_token_with_the_service_account_key(config, syslog_ng, port_allocator, service_account_key, auth_options):
    generator_source = config.create_example_msg_generator_source(num=1, template=config.stringify("test message"))
    pubsub_destination = create_google_destination(
        config, port_allocator, "google_pubsub",
        project=config.stringify("example-project"),
        topic=config.stringify("example-topic"),
        **auth_options(service_account_key),
    )
    config.create_logpath(statements=[generator_source, pubsub_destination])

    syslog_ng.start(config)

    [(headers, body)] = pubsub_destination.read_requests(1)
    claims = jwt_claims(headers)
    assert claims["iss"] == CLIENT_EMAIL
    assert claims["aud"] == PUBSUB_AUDIENCE
    assert "scope" not in claims

    [message] = json.loads(body)["messages"]
    assert base64.b64decode(message["data"]) == b"test message"


def test_google_pubsub_reports_each_gcp_auth_header_param_without_effect(config, syslog_ng, port_allocator, service_account_key):
    generator_source = config.create_example_msg_generator_source(num=1)
    pubsub_destination = create_google_destination(
        config, port_allocator, "google_pubsub",
        project=config.stringify("example-project"),
        topic=config.stringify("example-topic"),
        credentials=config.stringify(service_account_key),
        gcp_auth_header_params={"ca_dir": '"/nonexistent"', "scope": config.stringify(CLOUD_PLATFORM_SCOPE)},
    )
    config.create_logpath(statements=[generator_source, pubsub_destination])

    syslog_ng.start(config)

    pubsub_destination.read_requests(1)
    for option in ("ca-dir()", "scope()"):
        assert syslog_ng.is_message_in_console_log("option='{}'".format(option))


def test_google_pubsub_credentials_must_exist(config, syslog_ng, port_allocator):
    generator_source = config.create_example_msg_generator_source(num=1)
    pubsub_destination = create_google_destination(
        config, port_allocator, "google_pubsub",
        project=config.stringify("example-project"),
        topic=config.stringify("example-topic"),
        credentials=config.stringify("/nonexistent/service-account.json"),
    )
    config.create_logpath(statements=[generator_source, pubsub_destination])

    with pytest.raises(Exception) as exec_info:
        syslog_ng.start(config)
    assert "syslog-ng config syntax error" in str(exec_info.value)
    syntax_check_stderr = Path(syslog_ng.instance_paths.get_syntax_only_stderr_path()).read_text()
    assert 'File "/nonexistent/service-account.json" not found' in syntax_check_stderr


def test_google_pubsub_needs_credentials_or_auth(config, syslog_ng, port_allocator):
    generator_source = config.create_example_msg_generator_source(num=1)
    pubsub_destination = create_google_destination(
        config, port_allocator, "google_pubsub",
        project=config.stringify("example-project"),
        topic=config.stringify("example-topic"),
    )
    config.create_logpath(statements=[generator_source, pubsub_destination])

    with pytest.raises(Exception):
        syslog_ng.start(config)
    assert syslog_ng.wait_for_message_in_console_log("key() or credentials() is mandatory") != []

