#!/usr/bin/env ruby
require 'rubygems'
require 'msgpack/rpc'

port = 9991

pid = Process.fork
unless pid
	Process.exec "./mplinda #{port}"
	exit!
end

begin
	sleep 0.1

	c = MessagePack::RPC::Client.new('127.0.0.1', port)

	c.call(:push, "1")
	c.call(:push, "2")
	c.call(:push, "3")
	p c.call(:try_pop)  #=> "1"
	p c.call(:try_pop)  #=> "2"
	p c.call(:try_pop)  #=> "3"
	p c.call(:try_pop)  #=> nil

	c.call(:write, [1, "get", ["test.txt"]])
	c.call(:write, [0, "Hello, Linda!"])
	p c.call(:take, 10, [1, nil, nil])
	p c.call(:take, 10, [0, nil])

ensure
	Process.kill :TERM, pid
	Process.waitpid pid
end

