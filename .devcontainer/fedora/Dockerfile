FROM registry.fedoraproject.org/fedora:42

RUN dnf update -y && dnf -y --setopt=tsflags=nodocs install \
    clang \
    clang-tools-extra \
    cmake \
    make \
    gdb \
    rpmdevtools \
    git \
    openssl \
    openssl-devel \
    boost-devel \
    boost-static \
    && dnf clean all

ARG USERNAME=devel
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN groupadd --gid $USER_GID $USERNAME \
    && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME \
    && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME

USER $USERNAME
