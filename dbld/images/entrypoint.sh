#!/bin/bash

set -e

SOURCE_DIR=/source

# A linked git worktree is mounted at its host path (SOURCE_DIR_ON_HOST) and
# /source becomes a symlink to it: git records the worktree's and its
# submodules' paths relative to the host layout, and those only resolve through
# the real path. /source must not be a mount point for that -- an image that
# still declares VOLUME /source gives it an anonymous volume.
if [ -n "$SOURCE_DIR_ON_HOST" ]; then
    if [ -d $SOURCE_DIR ] && [ ! -L $SOURCE_DIR ] && ! rmdir $SOURCE_DIR 2>/dev/null; then
        echo "dbld: $SOURCE_DIR is a mount point, cannot make it a symlink to $SOURCE_DIR_ON_HOST" >&2
        echo "dbld: the image still declares VOLUME $SOURCE_DIR; rebuild it with './dbld/rules image-$IMAGE_PLATFORM' to build from a git worktree" >&2
        exit 1
    fi
    ln -sfn "$SOURCE_DIR_ON_HOST" $SOURCE_DIR
fi

USER_NAME=${USER_NAME_ON_HOST:-dockerguest}
USER_ID=`stat -L -c '%u' $SOURCE_DIR`
GROUP_NAME=$USER_NAME
GROUP_ID=`stat -L -c '%g' $SOURCE_DIR`

function create_user() {
    groupadd --gid $GROUP_ID $GROUP_NAME &>/dev/null || \
        groupadd --gid $GROUP_ID dockerguest &>/dev/null || \
        echo "Failed to add group $GROUP_NAME/$GROUP_ID in docker entrypoint-debian.sh";
    useradd $USER_NAME --uid=$USER_ID --gid=$GROUP_ID &>/dev/null || \
        useradd dockerguest --uid=$USER_ID --gid=$GROUP_ID &>/dev/null || \
        echo "Failed to add user $USER_NAME/$USER_ID in docker entrypoint-debian.sh";
    usermod -a -G sudo $USER_NAME || usermod -a -G wheel $USER_NAME
    sed -i -e '/^%sudo\s\+ALL=/s,ALL$,NOPASSWD: ALL,' /etc/sudoers
    sed -i -e '/^%wheel\s\+ALL=/s,ALL$,NOPASSWD: ALL,' /etc/sudoers
    mkdir -p /home/$USER_NAME
    chown $USER_NAME:$GROUP_ID /home/$USER_NAME
}

if [[ "$USER_ID" -eq 0 ]]; then
    "$@"
else
    if getent passwd $USER_ID > /dev/null
    then
        echo "USER_ID: $USER_ID already exist in passwd database, performing cleanup"
        userdel --remove $(getent passwd $USER_ID | cut -d":" -f1)
	create_user
    else
        create_user
    fi
    echo "Added new user: $USER_NAME"
    exec sudo --preserve-env --preserve-env=PATH -Hu "${USER_NAME}" "$@"
fi
