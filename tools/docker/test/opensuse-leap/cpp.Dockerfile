# Create a virtual environment with all tools installed
# ref: https://hub.docker.com/r/opensuse/leap
FROM opensuse/leap

# Install system build dependencies
ENV PATH=/usr/local/bin:$PATH
RUN zypper update -y \
&& zypper install -y git gcc11 gcc11-c++ \
 wget which lsb-release util-linux pkgconfig autoconf libtool zlib-devel \
&& zypper clean -a
ENV CC=gcc-11 CXX=g++-11
ENTRYPOINT ["/usr/bin/bash", "-c"]
CMD ["/usr/bin/bash"]

WORKDIR /root
ADD or-tools_amd64_opensuse-leap_v*.tar.gz .

RUN cd or-tools_*_v* && make test_cc
