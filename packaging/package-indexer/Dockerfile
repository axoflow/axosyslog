FROM ubuntu:noble

RUN apt-get update && apt-get install -y \
  python3-pip \
  apt-utils \
  gnupg2

RUN pip install --break-system-packages \
  azure-storage-blob \
  azure-mgmt-cdn \
  azure-identity \
  pyyaml \
  types-PyYAML \
  boto3 \
  types-requests
