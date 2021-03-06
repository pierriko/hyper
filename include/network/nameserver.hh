#ifndef _NETWORK_NAMESERVER_HH_
#define _NETWORK_NAMESERVER_HH_

#include <map>
#include <string>

#include <boost/noncopyable.hpp>
#include <boost/variant/variant.hpp>

#include <network/msg_name.hh>
#include <network/server_tcp_impl.hh>
#include <network/client_tcp_impl.hh>

namespace hyper {
	namespace network {

		// Internal details
		namespace ns {
			typedef boost::mpl::vector<
									  request_name, 
									  register_name
									> input_msg;
			typedef boost::mpl::vector<
									  request_name_answer,
									  register_name_answer
									 > output_msg;

			typedef boost::make_variant_over<input_msg>::type input_variant;
			typedef boost::make_variant_over<output_msg>::type output_variant;

			// for moment, only deal with tcp
			struct addr_storage {
				std::vector<boost::asio::ip::tcp::endpoint> tcp_endpoints;
			};

			class map_addr : private boost::noncopyable
			{
				typedef std::map<std::string, addr_storage> addrs;
				addrs map_;

				public:
					map_addr();
					bool add(const std::string& key, const addr_storage& s);
					bool remove(const std::string& key);
					bool isIn(const std::string& key) const;
					std::pair<bool, addr_storage> get(const std::string& key) const;
			};

		}

		namespace tcp {
			struct ns_port_generator
			{
				unsigned short base_port;

				ns_port_generator(const std::string& base_port);
				unsigned short get();
			};

			struct ns_visitor : public boost::static_visitor<ns::output_variant>
			{
				ns_visitor(ns::map_addr&, ns_port_generator&, std::ostream& );
				ns::output_variant operator() (const request_name& r) const;
				ns::output_variant operator() (const register_name& r) const;

				ns::map_addr& map_;
				ns_port_generator& gen_;
				std::ostream& output_;
			};

		}

		class name_server {
			typedef tcp::server<ns::input_msg, ns::output_msg, tcp::ns_visitor>
				tcp_ns_impl;

			std::iostream oss;
			ns::map_addr map_;
			tcp::ns_port_generator gen_;
			tcp_ns_impl tcp_ns_;

			public:
				name_server(const std::string&, const std::string&, 
							boost::asio::io_service&,
							bool verbose= false);
				void stop();
				void remove_entry(const std::string&);
		}; 

		struct name_resolve {
			request_name_answer rna;

			name_resolve() {};
			const std::string& name() { return rna.name; };
			void name(const std::string& name) { rna.name = name; } // a bit hackish
			const std::vector<boost::asio::ip::tcp::endpoint>& 
			endpoints() { return rna.endpoints; }
			bool success() { return rna.success; };
		};

		class name_client {
			typedef tcp::client<ns::output_msg> ns_client;
			ns_client client;
			boost::asio::io_service& io_s;
			bool is_resolving;

			template <typename Handler>
			void handle_resolve(const boost::system::error_code &e,
								name_resolve& solv,
								boost::tuple<Handler> handler)
			{
				is_resolving = false;
				if (e) {
					boost::get<0>(handler)(e);
				} else {
					if (solv.success()) {
						boost::get<0>(handler)(e);
					} else {
						// XXX wrong, return a real error from hyper::network::
						boost::get<0>(handler)(boost::system::error_code());
					}
				}
			}

			template <typename Handler>
			void async_resolve_(name_resolve & solv,
							   boost::tuple<Handler> handler)
			{
				/* 
				 * If we already are in the resolving state, (meaning that we
				 * are sent a name request, and we are waiting for an answer),
				 * just postpone the job. It will be scheduled later. Of
				 * course, if we are out of luck, it can be postponed until the
				 * end of the universe, but the probably is near 0.0
				 */
				if (is_resolving) {
					void (name_client::*postpone)(
									   name_resolve& solv,
									   boost::tuple<Handler>) =
						&name_client::template async_resolve_<Handler>;

					io_s.post(boost::bind(postpone, this, boost::ref(solv),
										  handler));
					return;
				}

				network::request_name rn;
				rn.name = solv.name();

				void (name_client::*f)(const boost::system::error_code& e,
									   name_resolve& solv,
									   boost::tuple<Handler>) =
					&name_client::template handle_resolve<Handler>;

				client.async_request(rn, solv.rna, 
						boost::bind(f, this, boost::asio::placeholders::error,
											 boost::ref(solv),
											 handler));
				is_resolving = true;
			}

			public:

			/*
			 * Can throw a boost::system::error_code if we can't connect
			 */
			name_client(boost::asio::io_service&, 
							 const std::string&, const std::string&);

			bool register_name(const std::string&, const std::vector<boost::asio::ip::tcp::endpoint>&);
			std::pair<bool, std::vector<boost::asio::ip::tcp::endpoint> > sync_resolve(const std::string&);

			template <typename Handler>
			void async_resolve(name_resolve & solv, Handler handler)
			{
				async_resolve_(solv, boost::make_tuple(handler));
			}
		};
	}
}

#endif /* _NETWORK_NAMESERVER_HH_ */
