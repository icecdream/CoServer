FROM centos:7.3.1611


RUN rm -f /etc/localtime \
  && ln -sv /usr/share/zoneinfo/Asia/Shanghai /etc/localtime \
  && echo "Asia/Shanghai" > /etc/timezone \
  && yum install -y gcc-c++ \
  && yum install -y make 

COPY ./ /home/CoServer


