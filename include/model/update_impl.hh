#ifndef HYPER_MODEL_UPDATE_IMPL_HH
#define HYPER_MODEL_UPDATE_IMPL_HH

#include <compiler/scope.hh>
#include <model/ability.hh>
#include <model/actor_impl.hh>
#include <model/update.hh>

namespace hyper {
	namespace model {

		namespace details {

			template <typename T>
			struct remote_args {
				typedef network::actor::remote_proxy<model::actor_impl> ability_remote_proxy;
					
				T& value_to_bind;
				ability_remote_proxy proxy;
				boost::optional<T> tmp;

				remote_args(model::ability& a, T& to_bind) :
					value_to_bind(to_bind), proxy(*a.actor) {}
			};

			template <typename T>
			void cb_proxy(const boost::system::error_code &e,
						  remote_args<T>* args,
						  updater::cb_type cb)
			{
				if (e) {
					delete args;
					return cb(e);
				}

				if (args->tmp) {
					args->value_to_bind = *(args->tmp);
					delete args;
					return cb(boost::system::error_code());
				}

				delete args;
				// XXX need better error handling
				return cb(boost::asio::error::invalid_argument); 
			}

			template <typename T>
			void call_proxy(ability &a, 
							const std::string& var, T& local_value, 
							network::identifier id, const std::string& src,
							updater::cb_type cb)
			{
				(void) id; (void) src;
				void (*f) (const boost::system::error_code&, 
						   remote_args<T>* args,
						   updater::cb_type cb) = cb_proxy<T>;

				remote_args<T>* args(new remote_args<T>(a, local_value));

				std::pair<std::string, std::string> p =
							compiler::scope::decompose(var);

				updater::cb_type local_cb = 
					boost::bind(f, boost::asio::placeholders::error, 
								args, cb);

				return args->proxy.async_get(p.first, p.second, args->tmp, local_cb);
			}
		}

		template <typename T>
		bool updater::add(const std::string& var, const std::string& remote_var,
						  T& local_value)
		{
			void (*f) (ability& a, 
					   const std::string& var, T& local_value,
					   network::identifier id, const std::string& src,
					   cb_type cb) = details::call_proxy<T>;
			// copy remote_var or we will have dangling reference
			fun_type fun = boost::bind(f, boost::ref(a), remote_var,
										  boost::ref(local_value), _1, _2, _3);
			std::pair<map_type::iterator, bool> p;
			p = map.insert(std::make_pair(var, fun));
			return p.second;
		}

		template <typename A>
		class update_variables<A, 0, boost::mpl::vector<> >
		{
			public:
				template <typename Handler>
				void async_update(Handler handler)
				{
					return handler(boost::system::error_code());
				}
		};

		template <typename A, size_t N>
		class update_variables<A, N, boost::mpl::vector<> >
		{
			private:
				A& a;
				local_vars update_status;

			public:
				update_variables(A& a, const boost::array<std::string, N>& update) :
					a(a), update_status(update) {}

				template <typename Handler>
				void async_update(Handler handler)
				{
					a.updater.async_update(update_status, 0, a.name, handler);
				}
		};

		template <typename A, typename vectorT>
		class update_variables<A, 0, vectorT> 
		{
			public:
				typedef network::actor::remote_values<vectorT> remote_values;
				typedef typename remote_values::seqReturn seqReturn;
				typedef typename remote_values::remote_vars_conf remote_vars_conf;

			private:
				network::actor::remote_proxy<model::actor_impl> proxy;
				remote_values remote;

			public:
				update_variables(A& a, const typename remote_values::remote_vars_conf& vars):
					proxy(*a.actor), remote(vars)
				{}

				template <typename Handler>
				void async_update(Handler handler)
				{
					proxy.async_get(remote, handler);
				}

				template<size_t i>
				typename boost::mpl::at<seqReturn, boost::mpl::int_<i> >::type
				at_c() const
				{
					return remote.at_c<i>();
				}
		};

		template <typename A, size_t N, typename vectorT>
		class update_variables 
		{
			public:
				typedef network::actor::remote_values<vectorT> remote_values;
				typedef typename remote_values::seqReturn seqReturn;
				typedef typename remote_values::remote_vars_conf remote_vars_conf;

			private:
				A& a;
				network::actor::remote_proxy<model::actor_impl> proxy;
				local_vars local_update_status;
				remote_values remote_update_status;

				template <typename Handler>
				void handle_update(const boost::system::error_code& e, 
								   boost::tuple<Handler> handler)
				{
					if (local_update_status.is_terminated() && 
						remote_update_status.is_terminated())
						boost::get<0>(handler)(e);
				}

			public:
				update_variables(A& a, const boost::array<std::string, N>& update,
								 const typename remote_values::remote_vars_conf& vars):
					a(a), proxy(*a.actor), local_update_status(update),
					remote_update_status(vars)
				{}
				
				template <typename Handler>
				void async_update(Handler handler)
				{
					void (update_variables::*f) (const boost::system::error_code& e, 
												 boost::tuple<Handler>)
						= &update_variables::template handle_update<Handler>;

					boost::function<void (const boost::system::error_code&)> 
					local_handler  =
						boost::bind(f, this, boost::asio::placeholders::error,
											 boost::make_tuple(handler));

					local_update_status.reset();
					remote_update_status.reset();
					a.updater.async_update(local_update_status, 0, a.name, local_handler);
					proxy.async_get(remote_update_status, local_handler);
				}

				template<size_t i>
				typename boost::mpl::at<seqReturn, boost::mpl::int_<i> >::type
				at_c() const
				{
					return remote_update_status.at_c<i>();
				}
		};
	}
}

#endif /* HYPER_MODEL_UPDATE_IMPL_HH */
