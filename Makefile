TARGET	= m46eapp m46ectl

COM_SRCS = \
	m46eapp_log.c \
	m46eapp_socket.c \

APP_SRCS = \
	m46eapp_main.c \
	m46eapp_network.c \
	m46eapp_config.c \
	m46eapp_tunnel.c \
	m46eapp_stub_network.c \
	m46eapp_backbone_mainloop.c \
	m46eapp_stub_mainloop.c \
	m46eapp_netlink.c \
	m46eapp_rtnetlink.c \
	m46eapp_command.c \
	m46eapp_setup.c \
	m46eapp_util.c \
	m46eapp_print_packet.c \
	m46eapp_statistics.c \
	m46eapp_hashtable.c \
	m46eapp_timer.c \
	m46eapp_pmtudisc.c \
	m46eapp_pr.c \
	m46eapp_dynamic_setting.c \
	m46eapp_mng_com_route.c \
	m46eapp_mng_v4_route.c \
	m46eapp_mng_v6_route.c \
	m46eapp_sync_com_route.c \
	m46eapp_sync_v4_route.c \
	m46eapp_sync_v6_route.c \

CTL_SRCS = \
	m46ectl.c \
	m46ectl_command.c \

COM_OBJS = $(COM_SRCS:.c=.o)
APP_OBJS = $(APP_SRCS:.c=.o)
CTL_OBJS = $(CTL_SRCS:.c=.o)

OBJS	= $(COM_OBJS) $(APP_OBJS) $(CTL_OBJS)
DEPENDS	= $(COM_SRCS:.c=.d) $(APP_SRCS:.c=.d) $(CTL_SRCS:.c=.d)
LIBS	= -lpthread -lrt

CC	= gcc
# for release flag
CFLAGS	= -O2 -Wall -std=gnu99 -D_GNU_SOURCE
# for debug flag
#CFLAGS	= -O2 -Wall -std=gnu99 -D_GNU_SOURCE -DDEBUG -g
# for debug flag
#CFLAGS	= -O2 -Wall -std=gnu99 -D_GNU_SOURCE -DDEBUG_SYNC -g
INCDIR	= -I.
LD	= gcc
LDFLAGS	= 
LIBDIR	=


all: $(TARGET)

cleanall:
	rm -f $(OBJS) $(DEPENDS) $(TARGET)

clean:
	rm -f $(OBJS) $(DEPENDS)

m46eapp: $(COM_OBJS) $(APP_OBJS)
	$(LD) $(LIBDIR) $(LDFLAGS) -o $@ $(COM_OBJS) $(APP_OBJS) $(LIBS)

m46ectl: $(COM_OBJS) $(CTL_OBJS)
	$(LD) $(LIBDIR) $(LDFLAGS) -o $@ $(COM_OBJS) $(CTL_OBJS)

.c.o:
	$(CC) $(INCDIR) $(CFLAGS) -c $<

%.d: %.c
	@set -e; $(CC) -MM $(CINCS) $(CFLAGS) $< \
		| sed 's/\($*\)\.o[ :]*/\1.o $@ : /g' > $@; \
		[ -s $@ ] || rm -f $@

-include $(DEPENDS)
