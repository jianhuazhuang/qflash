#!/bin/bash

dir_firehose=firehose
dir_fastboot=fastboot

build_firehose()
{
    if [ ! -d $dir_firehose ];
    then
        echo 'firehose directory is not found'
        read -n1 -p 'firehose is not found, firehose option can not be used! [Y/N]?' choice
        echo
        case $choice in
        Y|y)
            echo 'firehose will can not be used, README.md for more info';
            return 0;;
        N|n)
            echo 'fetch firehose source code and name the dir as firehose';
            return 1;;
        *)
            echo 'Unkonw choice' $choice;
            return 1;;
        esac;
    fi;
    
    cd $dir_firehose && make && cd ..
    return 0
}
    
build_fastboot()
{
    if [ ! -d $dir_fastboot ];
    then
        echo 'fastboot directory is not found'
        read -n1 -p 'fastboot is not found, fastboot option can not be used! [Y/N]?' choice
        echo
        case $choice in
        Y|y)
            echo 'fastboot will can not be used, README.md for more info';
            return 0;;
        N|n)
            echo 'fetch fastboot source code and name the dir as fastboot';
            return 1;;
        *)
            echo 'Unkonw choice' $choice;
            return 1;;
        esac;
    fi;
    
    cd $dir_fastboot && make && cd ..
    return 0
}

clean_firehose()
{
    if [ ! -d $dir_firehose ];
    then
        return 0
    fi;

    cd $dir_firehose && make && cd ..
}

clean_fastboot()
{
    if [ ! -d $dir_fastboot ];
    then
        return 0
    fi;

    cd $dir_fastboot && make clean && cd ..
}

if [[ $1 == "build_firehose" ]];
then
    build_firehose
elif [[ $1 == "clean_firehose" ]];
then
    clean_firehose
elif [[ $1 == "build_fastboot" ]];
then
    build_fastboot
elif [[ $1 == "clean_fastboot" ]];
then
    clean_fastboot
else
    echo "wrong parameter"
fi
