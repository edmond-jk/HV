#!/bin/bash

num_vec=${1-32}
buf_size=${2-1024}

ev3util /dev/ev3map0 noprompt fill_user_pattern 0x2121212121212121 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0x2121212121212121 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0x3434343434343434 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0x3434343434343434 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0x4343434343434343 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0x4343434343434343 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0x5656565656565656 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0x5656565656565656 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0x6565656565656565 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0x6565656565656565 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0x7878787878787878 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0x7878787878787878 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0x8787878787878787 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0x8787878787878787 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0x9a9a9a9a9a9a9a9a 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0x9a9a9a9a9a9a9a9a 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0xa9a9a9a9a9a9a9a9 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0xa9a9a9a9a9a9a9a9 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0xbcbcbcbcbcbcbcbc 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0xbcbcbcbcbcbcbcbc 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0xcbcbcbcbcbcbcbcb 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0xcbcbcbcbcbcbcbcb 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0xdededededededede 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0xdededededededede 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0xedededededededed 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0xedededededededed 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0xf0f0f0f0f0f0f0f0 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0xf0f0f0f0f0f0f0f0 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0x0f0f0f0f0f0f0f0f 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0x0f0f0f0f0f0f0f0f 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0x123456789abcdef0 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0x123456789abcdef0 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0xfedcba9876543210 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0xfedcba9876543210 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0xa5a5a5a5a5a5a5a5 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0xa5a5a5a5a5a5a5a5 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0x5a5a5a5a5a5a5a5a 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0x5a5a5a5a5a5a5a5a 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt fill_user_pattern 0x8172368947618923 0 0x40000000 $num_vec $buf_size
ev3util /dev/ev3map0 noprompt verify_user_pattern 0x8172368947618923 0 0x40000000 $num_vec $buf_size
