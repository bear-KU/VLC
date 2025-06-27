# Makefile
.PHONY: all diff clean

all: diff

parse_send_data:
	grep "^Data:" ./vlf_send/screenlog.0 | sed 's/^Data: //' > send_data.txt

parse_receive_data:
	grep "^Data:" ./vlf_receive/screenlog.0 | sed 's/^Data: //' > receive_data.txt

diff: parse_send_data parse_receive_data
	diff send_data.txt receive_data.txt && echo "Transmission successful!" || echo "Transmission failed."

clean:
	rm -f send_data.txt receive_data.txt
