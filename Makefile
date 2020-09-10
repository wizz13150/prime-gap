# Copyright 2020 Seth Troisi
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


OBJS     = gap_common.o modulo_search.o
OUT	 = gap_search gap_stats gap_test benchmark
CC	 = g++
FLAGS	 = -Wall -Werror -O3
LDFLAGS	 = -lgmp
PROGS	 = gap_search gap_test

%.o: %.cpp
	$(CC) -c -o $@ $< $(FLAGS)


benchmark: misc/benchmark.cpp modulo_search.o
	$(CC) -o $@ $^ $(FLAGS) -I. $(LDFLAGS)

$(PROGS) : %: %.cpp $(OBJS)
	$(CC) -o $@ $^ $(FLAGS) $(LDFLAGS)

gap_stats: gap_stats.cpp gap_common.o
	$(CC) -o $@ $^ $(FLAGS) $(LDFLAGS) -lsqlite3


.PHONY: clean

clean:
	rm -f $(OBJS) $(OUT)