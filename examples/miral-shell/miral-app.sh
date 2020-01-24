#!/usr/bin/env bash

miral_server=miral-shell
hostsocket=
bindir=$(dirname $0)

for terminal in gnome-terminal weston-terminal qterminal lxterminal x-terminal-emulator
do
  if which $terminal > /dev/null
  then break;
  fi
done

if [ "$(lsb_release -c -s)" != "xenial" ]
then
  qt_qpa=wayland
  gdk_backend=wayland
  sdl_videodriver=wayland
  enable_mirclient=""
else
  qt_qpa=ubuntumirclient
  gdk_backend=mir
  sdl_videodriver=mir
  enable_mirclient="--enable-mirclient"
fi

if [ -n "${MIR_SOCKET}" ]
then
  if [ ! -e "${MIR_SOCKET}" ]
  then
    echo "Error: Host endpoint '${MIR_SOCKET}' does not exist"; exit 1
  fi
  hostsocket='MIR_SERVER_HOST_SOCKET ${MIR_SOCKET}'
fi

socket=${XDG_RUNTIME_DIR}/miral_socket

port=0
while [ -e "${XDG_RUNTIME_DIR}/wayland-${port}" ]; do
    let port+=1
done
wayland_display=wayland-${port}

port=0

while [ -e "/tmp/.X11-unix/X${port}" ]; do
    let port+=1
done

x11_display=${port}

while [ $# -gt 0 ]
do
  if [ "$1" == "--help" -o "$1" == "-h" ]
  then
    echo "$(basename $0) - Handy launch script for a hosted miral \"desktop session\""
    echo "Usage: $(basename $0) [options] [shell options]"
    echo "Options are:"
    echo "    -kiosk                      use miral-kiosk instead of ${miral_server}"
    echo "    -terminal <terminal>        use <terminal> instead of '${terminal}'"
    echo "    -socket <socket>            set the legacy mir socket [${socket}]"
    echo "    -wayland-display <socket>   set the wayland socket [${wayland_display}]"
    echo "    -x11-display <socket>       set the X11 socket [${x11_display}]"
    echo "    -bindir <bindir>            path to the miral executable [${bindir}]"
    # omit    -demo-server as mir_demo_server is in the mir-test-tools package
    exit 0
  elif [ "$1" == "-kiosk" ];            then miral_server=miral-kiosk
  elif [ "$1" == "-terminal" ];         then shift; terminal=$1
  elif [ "$1" == "-socket" ];           then shift; socket=$1
  elif [ "$1" == "-wayland-display" ];  then shift; wayland_display=$1
  elif [ "$1" == "-x11-display" ];      then shift; x11_display=$1
  elif [ "$1" == "-bindir" ];           then shift; bindir=$1
  elif [ "$1" == "-demo-server" ];      then miral_server=mir_demo_server
  elif [ "${1:0:2}" == "--" ];          then break
  fi
  shift
done

if [ "${bindir}" != "" ]; then bindir="${bindir}/"; fi

if [ -e "${socket}" ]; then echo "Error: session endpoint '${socket}' already exists"; exit 1 ;fi
if [ -e "${XDG_RUNTIME_DIR}/${wayland_display}" ]; then echo "Error: wayland endpoint '${wayland_display}' already exists"; exit 1 ;fi

unset QT_QPA_PLATFORMTHEME

if [ "${miral_server}" == "miral-shell" ]
then
  # miral-shell can launch it's own terminal with Ctrl-Alt-T
  MIR_SERVER_FILE=${socket} WAYLAND_DISPLAY=${wayland_display} MIR_SERVER_X11_DISPLAY=${x11_display} MIR_SERVER_SHELL_TERMINAL_EMULATOR=${terminal} NO_AT_BRIDGE=1 ${hostsocket} exec dbus-run-session -- ${bindir}${miral_server} ${enable_mirclient} $*
else
  # miral-kiosk (and mir_demo_server) need a terminal launched
  if [ "${miral_server}" == "mir_demo_server" ]
  then
    MIR_SERVER_FILE=${socket} MIR_SERVER_X11_DISPLAY=${x11_display} WAYLAND_DISPLAY=${wayland_display} ${hostsocket} ${bindir}${miral_server} ${enable_mirclient} $*&
  else # miral-kiosk
    MIR_SERVER_FILE=${socket}                                       WAYLAND_DISPLAY=${wayland_display} ${hostsocket} ${bindir}${miral_server} ${enable_mirclient} $*&
  fi

  # Fixup for weird gnome-terminal script on Ubuntu
  if [ "${terminal}" == "gnome-terminal" ] && [ -e "/usr/bin/gnome-terminal.real" ]
  then
    terminal="gnome-terminal --disable-factory"
  fi

  while [ ! -e "${XDG_RUNTIME_DIR}/${wayland_display}" ]; do echo "waiting for ${wayland_display}"; sleep 1 ;done

  MIR_SOCKET=${socket} XDG_SESSION_TYPE=mir GDK_BACKEND=${gdk_backend} QT_QPA_PLATFORM=${qt_qpa} SDL_VIDEODRIVER=${sdl_videodriver} WAYLAND_DISPLAY=${wayland_display} NO_AT_BRIDGE=1 DISPLAY=:${x11_display} dbus-run-session -- ${terminal}
  killall ${bindir}${miral_server} || killall ${bindir}${miral_server}.bin
fi
