#!/bin/bash

set -e -u -o pipefail

if [[ $(id -u) -eq 0 ]]; then
  echo "You are trying to run build.sh as root, that won't work!"
  echo ""
  echo "If you get permission errors when running docker, check if you've finished the"
  echo "post installation steps and are member of \`docker\` system group."
  echo "See official docs: https://docs.docker.com/engine/install/linux-postinstall/"
fi

USAGE="Usage: $0 [--help] [--configure|--compile] [-j|--jobs {number_of_jobs}] {windows|linux} [cmake_flag...]"
export CONFIGURE=true
export COMPILE=true
export CMAKE_BUILD_PARALLEL_LEVEL=
export DOCKER_NO_USER_MOUNT=false
CMAKE_OPENXR_PATH="-DOpenXR_DIR=/usr/x86_64-w64-mingw32/cmake"
OS=
while (( $# > 0 )); do
  case $1 in
    --configure)
      CONFIGURE=true
      COMPILE=false
      shift
      ;;
    --no-user-mount)
      DOCKER_NO_USER_MOUNT=true
      shift
      ;;
    --compile)
      CONFIGURE=false
      COMPILE=true
      shift
      ;;
    --help)
      echo $USAGE
      echo "Options:"
      echo "  --help       print this help message"
      echo "  --configure  only configure, don't compile"
      echo "  --compile    only compile, don't configure"
      echo "  -j, --jobs   number of concurrent processes to use when building"
      exit 0
      ;;
    -j|--jobs)
      shift
      # Match numeric, starting with non-zero digit
      if ! [[ "${1-}" =~ ^[1-9]+[0-9]*$ ]]; then
        echo $USAGE
        exit 1
      fi
      CMAKE_BUILD_PARALLEL_LEVEL="$1"
      shift
      ;;
    windows|linux)
      OS="$1"
      shift
      # Add OpenXR path for Windows builds
      if [[ "$OS" == "windows" ]]; then
        set -- "$@" $CMAKE_OPENXR_PATH
      fi
      break
      ;;
    *)
      break
  esac
done
if [[ -z $OS ]]; then
  echo $USAGE
  exit 1
fi

cd "$(dirname "$(readlink -f "$0")")/.."
mkdir -p build-$OS .cache/ccache-$OS

# Use locally build image if available, and pull from upstream if not
image=recoil-build-amd64-$OS:latest
if [[ -z "$(docker images -q $image 2> /dev/null)" ]]; then
  image=ghcr.io/beyond-all-reason/recoil-build-amd64-$OS:latest
  docker pull $image
fi

MOUNT_PASSWD="-v /etc/passwd:/etc/passwd:ro"
MOUNT_GROUP="-v /etc/group:/etc/group:ro"

if [ "$DOCKER_NO_USER_MOUNT" = "true" ]; then
    MOUNT_PASSWD=""
    MOUNT_GROUP=""
fi

docker run -it --rm \
    $MOUNT_PASSWD \
    $MOUNT_GROUP \
    --user=$(id -u):$(id -g) \
    -v $(pwd):/build/src:ro \
    -v $(pwd)/.cache/ccache-$OS:/build/cache:rw \
    -v $(pwd)/build-$OS:/build/out:rw \
    -e CONFIGURE \
    -e COMPILE \
    -e CMAKE_BUILD_PARALLEL_LEVEL \
    $image \
    bash -c '
set -e
echo "$@"
cd /build/src/docker-build-v2/scripts
$CONFIGURE && ./configure.sh "$@"
if $COMPILE; then
  ./compile.sh
  # When compiling for windows, we must strip debug info because windows does
  # not handle the output binary size...
  if [[ $ENGINE_PLATFORM =~ .*windows ]]; then
    ./split-debug-info.sh
  fi
fi
' -- "$@"
