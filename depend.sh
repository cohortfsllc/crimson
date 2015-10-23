#!/bin/sh
DIR="$1"
CMP="$2"
FLAGS="$3"
shift 3
case "$DIR" in
    "" | ".")
	"$CMP" $FLAGS -MM -MG "$@" | sed -e 's@^\(.*\)\.o:@\1.d \1.o:@'
	;;
    *)
	"$CMP" $FLAGS -MM -MG "$@" | sed -e "s@^\(.*\)\.o:@$DIR/\1.d $DIR/\1.o:@"
	;;
esac
