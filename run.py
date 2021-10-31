#!/usr/bin/env python3
from scripts import *
import sys

class LeveldbDependencyChecker(DependencyChecker):
    def __init__(self, leveldb_option):
        super().__init__(['vefs:develop'], conf, leveldb_option)
        self.leveldb_option = leveldb_option
        
    def filter_files(self, list_of_files):
        return [f for f in list_of_files if f != './include/leveldb_autogen_conf.h']

    def process(self):
        shell.check_call("./build.sh '{}'".format(self.leveldb_option))

conf = {
    #"force_test": True,
}

if __name__ == "__main__":
    LeveldbDependencyChecker(sys.argv[1]).check_and_process()
