FROM debian:11
RUN apt-get update && apt-get install -y --no-install-recomends gcc make libreadline-dev libfuse3-dev
WORKDIR /opt

#RUN ["make", "deb"]
