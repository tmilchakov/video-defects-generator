FROM amd64/debian:stable-20230522@sha256:37d594c30a571bea519079ce524727e5550f675bdcba2ad1f69acff20553cb82
RUN apt-get update && apt-get install -y build-essential cmake libboost-all-dev \
      libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libavfilter-dev
WORKDIR /video-defects-generator
COPY CMakeLists.txt CMakeLists.txt
COPY main.cpp main.cpp

WORKDIR /video-defects-generator/build
RUN cmake  ..
RUN make -j 4
ENTRYPOINT ["./video-defects-generator"]
