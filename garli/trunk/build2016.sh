#! /bin/sh

main()
{
    MAKE_J="-j `nproc`"
    clone_ncl && (cd ncl && build_ncl) &&
        build_garli
}

nproc()
{
    if [ "$(uname -s)" = "Darwin" ]; then
        /usr/sbin/sysctl -n hw.logicalcpu
    else
        /usr/bin/nproc
    fi
}

# Clone and build ncl
clone_ncl()
{
    git submodule init &&
        git submodule update
}

build_ncl()
{
    ./bootstrap.sh &&
        env CXXFLAGS=-DNCL_CONST_FUNCS ./configure --prefix=`pwd`/installed --disable-shared --enable-static &&
        make $MAKE_J install
}

build_garli()
{
    ./bootstrap.sh &&
        ./configure "$@" --prefix=`pwd` --with-ncl=`pwd`/ncl/installed &&
        make $MAKE_J install
}

main
