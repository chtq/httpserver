#httpserver makefile
LIBINSTALLPATH = /usr/lib
HEADERINSTALLPATH = /usr/include

VERSION = 1.0.0

CC = gcc
CFLAGS = -g -Wall
CFLAGSAPI = -g -Wall -shared -Wl,-soname,libtobServAPI.so.$(VERSION)
LDFLAGS = -lpthread -ldl -lreadline -ltobFUNC -ltobCONF -ltobServAPI -ldbg
LDFLAGSAPI = -lpthread -ldl -lreadline -ltobFUNC -ltobCONF -ldbg

all: httpserver clean

httpserver: httpserver.o ModuleManager.o commandline.o
	$(CC) -o $@ $^ $(LDFLAGS)

API: FileCache.o Sessions.o PostVar.o GetVar.o Template.o tobServModule.o commandlineAPI.o
	$(CC) $(CFLAGSAPI) -o libtobServAPI.so.$(VERSION) $^ $(LDFLAGSAPI)

ModuleManager.o: ModuleManager.c ModuleManager.h
	$(CC) -c $(CFLAGS) $<

commandlineAPI.o: commandlineAPI.c commandlineAPI.h
	$(CC) -fPIC -c $(CFLAGS) $<

FileCache.o: FileCache.c FileCache.h Template.h
	$(CC) -fPIC -c $(CFLAGS) $<

Sessions.o: Sessions.c Sessions.h tobServModule.h
	$(CC) -fPIC -c $(CFLAGS) $<

PostVar.o: PostVar.c PostVar.h tobServModule.h
	$(CC) -fPIC -c $(CFLAGS) $<

GetVar.o: GetVar.c GetVar.h tobServModule.h
	$(CC) -fPIC -c $(CFLAGS) $<

Template.o: Template.c Template.h
	$(CC) -fPIC -c $(CFLAGS) $<

tobServModule.o: tobServModule.c tobServModule.h
	$(CC) -fPIC -c $(CFLAGS) $<

httpserver.o: httpserver.c tobServModule.h FileCache.h Sessions.h ModuleManager.h ModuleManager.o
	$(CC) -c $(CFLAGS) $<

installAPI: libtobServAPI.so.$(VERSION)
	cp libtobServAPI.so.$(VERSION) $(LIBINSTALLPATH)
	cp *.h $(HEADERINSTALLPATH)
	ln -sf $(LIBINSTALLPATH)/libtobServAPI.so.$(VERSION) $(LIBINSTALLPATH)/libtobServAPI.so

clean:
	touch ~.o
	rm *.o

cleanest: clean
	rm httpserver
	rm libtobServAPI.*
