#!/bin/sh
# Pull a Docker image for CI, surviving any one registry's limits, and tag it
# under a local name the job then runs.
#
#   sh tools/ci-pull.sh [--platform P] <image> <local-tag>
#
# An official image named bare (`alpine:3.21`, `ubuntu:22.04`) is the same
# content on several registries, and each of them refuses anonymous pulls from
# shared runners now and then: Docker Hub rate-limits them (429), which is why
# the jobs moved to ECR Public's mirror -- and ECR Public then answered
# `toomanyrequests: Data limit exceeded`, which cost release.yml's riscv64 job
# on 2026-10-16 and its x86_64 musl job on 2026-10-17, with nothing wrong in
# the tree. So this tries ECR Public, Google's Docker Hub mirror and Docker Hub
# itself in turn, for three rounds with a pause between them, and takes the
# first that answers.
#
# An image that names its registry (`quay.io/pypa/manylinux_2_28_x86_64`) has
# no second source; it gets the retries alone.
#
# Prints the registry prefix that served the image (`mirror.gcr.io/library/`,
# or nothing for an image that named its own), so a `docker build` can take
# its FROM from the same place. Docker's own progress goes to stderr.
#
# CI_PULL_MIRRORS overrides the list, for testing the fallback.

set -u

platform=''
if [ "${1-}" = --platform ]; then
    platform=${2-}
    shift 2
fi
image=${1-}
local_tag=${2-}
if [ -z "$image" ] || [ -z "$local_tag" ] || [ $# -ne 2 ]; then
    echo 'usage: sh tools/ci-pull.sh [--platform P] <image> <local-tag>' >&2
    exit 2
fi

case "$image" in
    */*)
        case "${image%%/*}" in
            *.*|*:*|localhost) mirrors='-' ;;   # names its host: that one alone
            *) mirrors=${CI_PULL_MIRRORS-'mirror.gcr.io/ docker.io/'} ;;   # a Docker Hub user's image
        esac ;;
    *)  mirrors=${CI_PULL_MIRRORS-'public.ecr.aws/docker/library/ mirror.gcr.io/library/ docker.io/library/'} ;;
esac

round=1
while [ "$round" -le 3 ]; do
    for prefix in $mirrors; do
        [ "$prefix" = - ] && prefix=''
        ref="$prefix$image"
        echo "ci-pull: $ref${platform:+ ($platform)}, round $round" >&2
        if docker pull ${platform:+--platform "$platform"} "$ref" >&2; then
            docker tag "$ref" "$local_tag" >&2 || exit 1
            printf '%s\n' "$prefix"
            exit 0
        fi
    done
    [ "$round" -lt 3 ] && sleep $((round * 30))
    round=$((round + 1))
done
echo "ci-pull: no registry would serve $image after 3 rounds" >&2
exit 1
