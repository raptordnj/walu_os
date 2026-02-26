FROM rust:1.77-bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
    make \
    gcc \
    binutils \
    grub-pc-bin \
    grub-common \
    xorriso \
    mtools \
    qemu-system-x86 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN rustup target add x86_64-unknown-none

# Provide the expected bare-metal toolchain command names.
RUN ln -sf /usr/bin/gcc /usr/local/bin/x86_64-elf-gcc && \
    ln -sf /usr/bin/ld /usr/local/bin/x86_64-elf-ld && \
    ln -sf /usr/bin/ar /usr/local/bin/x86_64-elf-ar && \
    ln -sf /usr/bin/objcopy /usr/local/bin/x86_64-elf-objcopy

WORKDIR /workspace

CMD ["make", "run-headless"]
