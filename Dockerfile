FROM debian:11

RUN apt-get update && apt-get install -y \
    gcc make libreadline-dev libfuse3-dev dpkg-dev

COPY . /opt/
WORKDIR /opt

RUN make build && make deb

RUN test -f kubsh && test -f kubsh_*.deb
