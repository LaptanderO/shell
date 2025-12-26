FROM debian:11

RUN apt-get update && apt-get install -y \
    gcc make libreadline-dev libfuse3-dev dpkg-dev

COPY . /opt/
WORKDIR /opt

RUN make build && make deb && \
    dpkg -i kubsh_*.deb || apt-get install -f -y  # ← УСТАНАВЛИВАЕТ!

RUN which kubsh && \
    ls -la /usr/bin/kubsh

CMD ["kubsh"]
