all:	nx584-sms


nx584-sms:	Makefile nx584-sms.c code_instrumentation.c serial.c
	gcc -g -Wall -o nx584-sms nx584-sms.c code_instrumentation.c serial.c
