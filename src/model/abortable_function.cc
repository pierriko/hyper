#include <model/abortable_function.hh>

#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>

namespace hyper {
	namespace model {
		const char* exec_layer_category_impl::name() const {
			return "exec_layer";
		}

		std::string exec_layer_category_impl::message(int ev) const
		{
			switch(ev) {
				case exec_layer_error::ok :
					return "ok";
				case exec_layer_error::interrupted:
					return "interrupted";
				case exec_layer_error::execution_failed:
					return "execution_failed";
				case exec_layer_error::execution_ko:
					return "execution_ko";
				case exec_layer_error::temporary_failure:
					return "temporary_failure";
				case exec_layer_error::run_again:
					return "run_again";
				default:
					return "unknow_error";
			}
		}

		abortable_function::abortable_function(exec_type exec, 
				abort_type abort,  err_type error):
				exec_(exec), abort_(abort), error_(error), running(false),
				must_interrupt(false), must_pause(false) {}

		void abortable_function::handler(const boost::system::error_code& e, cb_type cb)
		{
			if (must_interrupt) {
				running = false;
				return cb(make_error_code(boost::system::errc::interrupted));
			}

			if (must_pause)
				return;

			running = false;
			cb(e);
		}

		void abortable_function::compute (cb_type cb) {
			cb_ = cb;
			must_interrupt = false;
			running = true;
			if (!must_pause)
				return exec_(boost::bind(&abortable_function::handler, this, _1, cb));
		}

		bool abortable_function::abort() {
			must_interrupt = true;
			if (running)
				return abort_();
			else 
				return false;
		}

		void abortable_function::pause() { must_pause = true; if (running) abort_(); }

		void abortable_function::resume() { must_pause = false; if (running) compute(cb_); }


		const boost::system::error_category& exec_layer_category()
		{
			static exec_layer_category_impl instance;
			return instance;
		}

		void abortable_computation::terminaison(const boost::system::error_code& e)
		{
			if (wait_terminaison) return;

			std::vector<bool> b;
			std::transform(seq.begin(), seq.end(), std::back_inserter(b),
						   boost::bind(&abortable_function_base::abort, _1));

			wait_terminaison = true;
			err = e;
			still_pending = std::count(b.begin(), b.end(), true);

			if (!still_pending) cb(e);
		}

		bool abortable_computation::check_is_terminated(const boost::system::error_code& e)
		{
			if (!wait_terminaison)
				return false;

			if (e == boost::system::errc::interrupted) {
				still_pending--;
				if (!still_pending)
					cb(err);
				return true;
			}

			return false;
		}

		void abortable_computation::handle_computation(const boost::system::error_code& e, size_t idx)
		{
			if (check_is_terminated(e)) return;

			if (e == boost::system::errc::interrupted) {
				/* Check if idx is in the requested_abort. If it is not the
				 * case, just notice that something wrong happen. Otherwise,
				 * call the associated callback. Don't erase the entry now,
				 * because we don't know exactly when the callback is
				 * terminated, and so it can produce weird things
				 */
				interrupt_map::iterator it = requested_abort.find(idx);
				if (it != requested_abort.end()) 
					it->second(e);
				return;
			}

			/* 
			 * If we get a temporary failure, we need to pause each primitive
			 * between the currently executed one (represented by index  and
			 * the one which fails (represented by idx). If we get the continue
			 * message, we need to do the contrary.  In both case, don't care
			 * about idx one.  pause() and resume() must do nothing if the
			 * action has already terminated
			 *
			 * idx is guarantee to be <= to index so (index - idx) >= 0
			 * so the size_t is valid 
			 */
			if (e == make_error_code(exec_layer_error::temporary_failure)) {
				for (size_t i = 0; i < (size_t)(index - idx); ++i)
					seq[index - i]->pause();
				return;
			}

			if (e == make_error_code(exec_layer_error::run_again)) {
				for (size_t i = idx+1; i <= index; ++i)
					seq[i]->resume();
				return;
			}

			if (e) {
				error_index = idx;
				terminaison(e);
			} else {
				/* just execute the next instruction, if any */
				if (index+1 == seq.size())
					return terminaison(boost::system::error_code());
				else {
					index++;
					if (must_pause)
						seq[index]->pause();
					seq[index]->compute(boost::bind(
								&abortable_computation::handle_computation,
								this, boost::asio::placeholders::error, index));
				}
			}
		}

		void abortable_computation::compute(cb_type cb_) 
		{
			if (seq.empty()) 
				return cb_(boost::system::error_code());

			cb = cb_;
			index = 0;
			error_index = boost::none;
			wait_terminaison = false;

			if (must_pause)
				seq[index]->pause();
			seq[index]->compute(boost::bind(&abortable_computation::handle_computation,
						this, boost::asio::placeholders::error, index));
		}

		network::runtime_failure abortable_computation::error() const {
			assert(error_index);
			return seq[*error_index]->error();
		}

		void abortable_computation::abort() 
		{
			return terminaison(make_error_code(exec_layer_error::interrupted));
		}

		void abortable_computation::abort(size_t idx, cb_type cb_)
		{
			requested_abort[idx] = cb_;
			seq[idx]->abort();
		}

		void abortable_computation::pause()
		{
			must_pause = true;
			for (ssize_t i = index; i >= 0; --i)
				seq[i]->pause();
		}

		void abortable_computation::resume()
		{
			must_pause = false;
			for (size_t i = 0; i <= index; ++i)
				seq[i]->resume();
		}
	}
}
