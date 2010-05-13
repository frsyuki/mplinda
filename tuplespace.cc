//
// mplinda
//
// Copyright (C) 2010 FURUHASHI Sadayuki
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.
#include <msgpack/rpc/server.h>
#include <mp/functional.h>
#include <mp/sync.h>
#include <list>

#define GET_ARGS(...) \
	msgpack::type::tuple<__VA_ARGS__> args; \
	req.params().convert(&args);

class TupleSpace : public msgpack::rpc::dispatcher {
public:
	static const double TIMER_INTERVAL = 0.5;

	TupleSpace(msgpack::rpc::loop loop)
	{
		loop->add_timer(TIMER_INTERVAL, TIMER_INTERVAL,
				mp::bind(&TupleSpace::on_timer, this));
		nil.type = msgpack::type::NIL;
	}

	~TupleSpace() { }

	typedef msgpack::rpc::shared_zone shared_zone;
	typedef msgpack::rpc::request request;

	void dispatch(request req)
	try {
		std::string method = req.method().as<std::string>();

		if(method == "write") {
			GET_ARGS(msgpack::object);
			write(req, args.get<0>());

		} else if(method == "push") {
			GET_ARGS(msgpack::object);
			push(req, args.get<0>());

		} else if(method == "take") {
			GET_ARGS(unsigned int, msgpack::object);
			take(req, args.get<0>(), args.get<1>());

		} else if(method == "pop") {
			GET_ARGS(unsigned int);
			pop(req, args.get<0>());

		} else if(method == "try_take") {
			GET_ARGS(msgpack::object);
			try_take(req, args.get<0>());

		} else if(method == "try_pop") {
			GET_ARGS();
			try_pop(req);

		} else if(method == "read"){
			GET_ARGS(unsigned int, msgpack::object);
			read(req, args.get<0>(), args.get<1>());

		} else if(method == "copy") {
			GET_ARGS(unsigned int);
			copy(req, args.get<0>());

		} else if(method == "try_read") {
			GET_ARGS(msgpack::object);
			try_read(req, args.get<0>());

		} else if(method == "try_copy") {
			GET_ARGS();
			try_copy(req);
		}

	} catch (std::exception& e) {
		req.error( std::string(e.what()) );
	}

private:
	msgpack::object nil;

	void write(request req,
			msgpack::object obj)
	{
		push_impl(req, obj);
	}

	void push(request req,
			msgpack::object obj)
	{
		push_impl(req, obj);
	}

	void take(request req,
			unsigned int timeout_sec,
			msgpack::object pattern)
	{
		pop_impl(req, pattern, timeout_sec, true);
	}

	void pop(request req,
			unsigned int timeout_sec)
	{
		pop_impl(req, nil, timeout_sec, true);
	}

	void try_take(request req,
			msgpack::object pattern)
	{
		pop_impl(req, pattern, 0, true);
	}

	void try_pop(request req)
	{
		pop_impl(req, nil, 0, true);
	}

	void read(request req,
			unsigned int timeout_sec,
			msgpack::object pattern)
	{
		pop_impl(req, pattern, timeout_sec, false);
	}

	void copy(request req,
			unsigned int timeout_sec)
	{
		pop_impl(req, nil, timeout_sec, false);
	}

	void try_read(request req,
			msgpack::object pattern)
	{
		pop_impl(req, pattern, 0, false);
	}

	void try_copy(request req)
	{
		pop_impl(req, nil, 0, false);
	}

private:
	void push_impl(request req,
			msgpack::object obj)
	{
		sync_t::ref ref(m_sync);

		if(!ref->request_queue.empty()) {
			for(request_queue_t::iterator re(ref->request_queue.begin());
					re != ref->request_queue.end(); ++re) {
				if(!match(obj, re->pattern)) { continue; }
				re->req.result(obj, req.zone());
				ref->request_queue.erase(re);
				req.result_nil();
				return;
			}
		}

		shared_zone life(req.zone().release());
		object_entry o = {obj, life};
		ref->object_queue.push_back(o);

		req.result_nil();
	}

	void pop_impl(request req,
			msgpack::object pattern,
			unsigned int timeout_sec,
			bool remove = true)
	{
		sync_t::ref ref(m_sync);

		if(!ref->object_queue.empty()) {
			for(object_queue_t::iterator oe(ref->object_queue.begin());
					oe != ref->object_queue.end(); ++oe) {
				if(!match(oe->obj, pattern)) { continue; }
				req.result(oe->obj, oe->life);
				if(remove) {
					ref->object_queue.erase(oe);
				}
				return;
			}
		}

		if(timeout_sec == 0) {
			req.result_nil();
			return;
		}

		unsigned int timeout = timeout_sec / TIMER_INTERVAL;
		shared_zone life(req.zone().release());
		request_entry r = {timeout, req, pattern, life};
		ref->request_queue.push_back(r);
	}

	bool match(msgpack::object obj, msgpack::object pattern)
	{
		if(pattern.is_nil()) { return true; }
		if(pattern.type != msgpack::type::ARRAY) { return false; }
		if(obj.type != msgpack::type::ARRAY) { return false; }

		msgpack::object_array o = obj.via.array;
		msgpack::object_array p = pattern.via.array;

		if(o.size < p.size) {
			return false;
		}

		for(unsigned int i = 0; i < p.size; ++i) {
			if(!p.ptr[i].is_nil() && o.ptr[i] != p.ptr[i]) {
				return false;
			}
		}
		return true;
	}

	bool on_timer()
	{
		sync_t::ref ref(m_sync);
		for(request_queue_t::iterator re(ref->request_queue.begin());
				re != ref->request_queue.end(); ) {
			if(--re->timeout == 0) {
				re->req.result_nil();
				re = ref->request_queue.erase(re);
			} else {
				++re;
			}
		}
		return true;
	}

private:
	struct request_entry {
		unsigned int timeout;
		msgpack::rpc::request req;
		msgpack::object pattern;
		shared_zone life;
	};

	struct object_entry {
		msgpack::object obj;
		shared_zone life;
	};

	typedef std::list<request_entry> request_queue_t;
	typedef std::list<object_entry> object_queue_t;

	struct sync_set {
		request_queue_t request_queue;
		object_queue_t object_queue;
	};

	typedef mp::sync<sync_set> sync_t;
	sync_t m_sync;

private:
	TupleSpace();
	TupleSpace(const TupleSpace&);
};


msgpack::rpc::server svr;

bool signal_stop()
{
	svr.end();
	return true;
}

int main(int argc, char* argv[])
try {
	if(argc != 2) {
		std::cout << "usage: "<<argv[0]<<" <port>" << std::endl;
		exit(1);
	}

	unsigned short port = atoi(argv[1]);

	svr.get_loop()->add_signal(SIGTERM, signal_stop);
	svr.get_loop()->add_signal(SIGINT,  signal_stop);

	std::auto_ptr<TupleSpace> qs(new TupleSpace(svr.get_loop()));

	svr.serve(qs.get());
	svr.listen("0.0.0.0", port);

	std::cout << "start on 0.0.0.0:" << port << std::endl;

	svr.run(4);

	std::cout << "end." << std::endl;

} catch (std::exception& e) {
	std::cout << "error: "<<e.what()<<std::endl;
}

