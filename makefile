TARGET = jnXssh
OBJS = main.o page.o site.opp debug.o

HEADER = config.h
CFLAGS = -g -Wall -pipe $(shell pkg-config --cflags gtk+-3.0 vte-2.90 gthread-2.0 tinyxml)
CXXFLAGS = -g -Wall -pipe $(shell pkg-config --cflags gtk+-3.0 vte-2.90 gthread-2.0 tinyxml)
LDFLAGS += -lstdc++ $(shell pkg-config --libs gtk+-3.0 vte-2.90 gthread-2.0 tinyxml)

$(TARGET): $(OBJS)
	gcc $^ $(LDFLAGS) -o $@

%.o: %.c %.h ${HEADER} makefile
	gcc -c $(CFLAGS) -o $@ $<

%.opp: %.cpp %.h ${HEADER} makefile
	gcc -c $(CXXFLAGS) -o $@ $<

install: ${TARGET}
	sudo desktop-file-install jnXssh.desktop
	sudo cp ${TARGET} /usr/local/bin/
	mkdir -p ${HOME}/.jnXssh/
	cp -r res ${HOME}/.jnXssh/
	if [ ! -f ${HOME}/.jnXssh/site.xml ]; then cp site.xml.tmpl ${HOME}/.jnXssh/site.xml; fi

clean:
	rm -rf $(TARGET) $(OBJS)
	rm -rf cscope*

