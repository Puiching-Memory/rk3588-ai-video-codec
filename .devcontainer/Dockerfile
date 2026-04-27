FROM nvidia/cuda:12.4.1-cudnn-devel-ubuntu22.04

ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=$USER_UID

ENV DEBIAN_FRONTEND=noninteractive
ENV UV_INSTALL_DIR=/usr/local/bin

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        curl \
        git \
        python3 \
        python3-pip \
        python3-venv \
        sudo \
    && rm -rf /var/lib/apt/lists/*

RUN curl -LsSf https://astral.sh/uv/install.sh | sh

RUN groupadd --gid ${USER_GID} ${USERNAME} \
    && useradd --uid ${USER_UID} --gid ${USER_GID} -m ${USERNAME} \
    && echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME} \
    && chmod 0440 /etc/sudoers.d/${USERNAME}

USER ${USERNAME}
