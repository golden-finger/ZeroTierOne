# Dockerfile for ZeroTier Central Controllers Building

FROM ubuntu as build
MAINTAINER Jiang Rensheng <jiangrs@deepsecs.com>

ENV DEBIAN_FRONTEND=noninteractive

COPY sources.aliyun /etc/apt/sources.list

RUN apt-get update -y && \
    apt-get install -y git-core build-essential 

WORKDIR /opt
RUN git clone https://github.com/golden-finger/ZeroTierOne.git

WORKDIR /opt/ZeroTierOne

COPY make-linux.mk /opt/ZeroTierOne/make-linux.mk
RUN make central-controller


FROM ubuntu

ENV DEBIAN_FRONTEND=noninteractive

COPY sources.aliyun /etc/apt/sources.list
RUN apt-get update -y

COPY --from=build /opt/ZeroTierOne/zerotier-one /usr/local/bin/zerotier-one
RUN chmod a+x /usr/local/bin/zerotier-one

ADD main.sh /
RUN chmod a+x /main.sh

RUN apt-get install -y libjemalloc1

ENTRYPOINT /main.sh 
