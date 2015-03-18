#summary How to create private development branch
#labels Phase-Implementation

```
svn co https://amv-codec-tools.googlecode.com/svn/ amv-codec-tools
cd amv-codec-tools

# create private branch for dpavlin
svn cp trunk branches/dpavlin/

# create local ffmpeg tree
svn cp branches/ffmpeg/ branches/dpavlin/AMVmuxer/ffmpeg/

# create patched version from current diff
make patch

# build ffmpeg
make build
```