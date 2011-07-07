#include <model/ability_test.hh>
#include <model/future.hh>

using namespace hyper::model;

void dont_care(const boost::system::error_code& e)
{}

void ability_test::handle_send_constraint(const boost::system::error_code& e,
		future_value<bool> res,
		network::request_constraint* msg,
		network::request_constraint_answer* ans)
{
	if (e) {
		std::cerr << "Failed to make " << msg->constraint << std::endl;
	} else {
		if (ans->success == true) {
			std::cerr << "Succesfully enforcing " << msg->constraint << std::endl;
		} else {
			std::cerr << "Failed to enforce " << msg->constraint << std::endl;
		}
	}
	res.get_raw() = (!e && ans->success);
	res.signal_ready();

	delete msg;
	delete ans;
}


future_value<bool> ability_test::send_constraint(const std::string& constraint, bool repeat)
{
	network::request_constraint* msg(new network::request_constraint());
	network::request_constraint_answer *answer(new network::request_constraint_answer());
	msg->constraint = constraint;
	msg->repeat = repeat;

	future_value<bool> res(constraint);

	client_db[target].async_request(*msg, *answer, 
			boost::bind(&ability_test::handle_send_constraint,
				this,
				boost::asio::placeholders::error,
				res, msg, answer));

	return res;
}

void ability_test::abort(const std::string& msg)
{
	network::terminate term(msg);
	client_db[target].async_write(term, &dont_care);
}
