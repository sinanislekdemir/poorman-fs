# poorman-fs
FUSE File System implementation to PoorMan's Catalog Creator

[PoorMan](https://github.com/sinanislekdemir/poorman) is a DVD/CD/External Drive
indexing software. It can index your cold-storages so you can search what you need
in your cold storages without physically attaching them to your computer.

And PoorManFS is a FUSE implementation to PoorMan database.

You can mount the index as a "virtual" drive and use your favorite search tool
or browse the files easily without really attaching to the disks.

poormanfs works on the user-level. So no root access is needed;

[See it in action on youtube](https://www.youtube.com/watch?v=EUga7YicvKs)

## Install

### Install from the Debian Package

Deb package can be found here:
https://github.com/sinanislekdemir/poorman-fs/releases

### Install from the source code
How to compile and run?

This software depends on `libsqlite3-dev` and `libfuse-dev` so make sure you have them first!

```
git clone https://github.com/sinanislekdemir/poorman-fs.git
cd poorman-fs
make
sudo make install
poormanfs /path/to/mount
```

for your convenience.

As an example:

```
mkdir /tmp/testdrive
poormanfs /tmp/testdrive
ls /tmp/testdrive
```
