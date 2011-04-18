#include <model/ability.hh>
#include <model/compute_make_expression.hh>

namespace hyper {
	namespace model {
		compute_make_expression::compute_make_expression(
			ability& a, const std::string& dst, const std::string& ctr, bool& res) : 
			a(a), dst(dst), constraint(ctr), res(res), id(boost::none) {}

		void compute_make_expression::handle_end_computation(
				const boost::system::error_code& e,
				cb_type cb)
		{
			if (e) 
				cb(e);

			res = ans.success;
			if (res) {
				cb(boost::system::error_code());
			} else {
				cb(make_error_code(exec_layer_error::execution_ko));
			}
		}

		void compute_make_expression::compute(cb_type cb) 
		{
			id = boost::none;
			rqst.constraint =  constraint;

			id = a.client_db[dst].async_request(rqst, ans, 
					boost::bind(&compute_make_expression::handle_end_computation,
								this, boost::asio::placeholders::error, cb));
		}

		void compute_make_expression::handle_abort(const boost::system::error_code&)
		{}

		void compute_make_expression::abort() 
		{
			if (!id)
				return;

			abort_msg.id = *id;
			a.client_db[dst].async_write(abort_msg, 
						boost::bind(&compute_make_expression::handle_abort,
									 this, boost::asio::placeholders::error));

		}
	}
}