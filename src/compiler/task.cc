#include <compiler/ability.hh>
#include <compiler/condition_output.hh>
#include <compiler/depends.hh>
#include <compiler/logic_expression_output.hh>
#include <compiler/exec_expression_output.hh>
#include <compiler/output.hh>
#include <compiler/scope.hh>
#include <compiler/task.hh>
#include <compiler/task_parser.hh>
#include <compiler/types.hh>
#include <compiler/universe.hh>
#include <compiler/utils.hh>

#include <boost/bind.hpp>

namespace {
	using namespace hyper::compiler;
	
	struct generate_logic_fact {
		std::string name;
		std::ostream &oss;

		generate_logic_fact(const std::string& name_, std::ostream& oss_):
			name(name_), oss(oss_)
		{}

		void operator() (const expression_ast& e) const
		{
			oss << "\t\t\ta_.logic().engine.add_fact(";
			oss << quoted_string(generate_logic_expression(e));
			oss << ", " << quoted_string(name) <<  ");" << std::endl;
		}
	};

	struct cond_validate
	{
		bool& res;
		const ability& ab;
		const universe& u;

		cond_validate(bool &res_, const ability& ab_, const universe& u_):
			res(res_), ab(ab_), u(u_)
		{}

		void operator() (const expression_ast& cond) 
		{
			res = cond.is_valid_predicate(ab, u, boost::none) && res;
		}
	};
}

namespace hyper {
	namespace compiler {

			task::task(const task_decl& decl, const ability& a_, const typeList& tList_):
				name(decl.name), pre(decl.conds.pre.list), post(decl.conds.post.list),
				ability_context(a_), tList(tList_) 
			{}

			bool task::validate(const universe& u) const
			{
				bool res = true;
				{
				cond_validate valid(res, ability_context, u);
				std::for_each(pre.begin(), pre.end(), valid); 
				res = valid.res && res;
				}
				{
				cond_validate valid(res, ability_context, u);
				std::for_each(post.begin(), post.end(), valid); 
				res = valid.res && res;
				}
				return res;
			}

			void task::dump_include(std::ostream& oss, const universe& u) const
			{
				const std::string indent="\t\t";
				const std::string next_indent = indent + "\t";

				std::string exported_name_big = exported_name();
				std::transform(exported_name_big.begin(), exported_name_big.end(), 
							   exported_name_big.begin(), toupper);
				guards g(oss, ability_context.name(), exported_name_big + "_HH");

				oss << "#include <model/task.hh>" << std::endl;

				namespaces n(oss, ability_context.name());
				oss << indent << "struct ability;" << std::endl;
				oss << indent << "struct " << exported_name();
				oss << " : public model::task {" << std::endl;

				oss << next_indent << "hyper::model::evaluate_conditions<";
				oss << pre.size() << ", ability> preds;" << std::endl; 

				oss << next_indent << exported_name();
				oss << "(ability& a_);" << std::endl; 
				oss << next_indent;
				oss << "void async_evaluate_preconditions(model::condition_execution_callback cb);";
				oss << std::endl;

				oss << indent << "};" << std::endl;
			}

			void task::dump(std::ostream& oss, const universe& u) const
			{
				const std::string indent="\t\t";
				const std::string next_indent = indent + "\t";

				depends deps;
				void (*f)(const expression_ast&, const std::string&, depends&) = &add_depends;
				std::for_each(pre.begin(), pre.end(),
							  boost::bind(f ,_1, boost::cref(ability_context.name()),
												 boost::ref(deps)));
				std::for_each(post.begin(), post.end(),
							  boost::bind(f ,_1, boost::cref(ability_context.name()),
												 boost::ref(deps)));
				
				oss << "#include <" << ability_context.name();
				oss << "/ability.hh>" << std::endl;
				oss << "#include <" << ability_context.name();
				oss << "/tasks/" << name << ".hh>" << std::endl;

				oss << "#include <boost/assign/list_of.hpp>" << std::endl;

				std::for_each(deps.fun_depends.begin(), 
							  deps.fun_depends.end(), dump_depends(oss, "import.hh"));
				oss << std::endl;

				{
				anonymous_namespaces n(oss);
				exec_expression_output e_dump(u, ability_context, *this, oss, tList, pre.size(), "pre_");
				std::for_each(pre.begin(), pre.end(), e_dump);
				}

				namespaces n(oss, ability_context.name());

				/* Generate constructor */
				oss << indent << exported_name() << "::" << exported_name();
				oss << "(hyper::" << ability_context.name() << "::ability & a_) :" ;
				oss << "model::task(a_, " << quoted_string(name);
				oss << "),\n";
				oss << indent << "preds(a_, boost::assign::list_of<hyper::model::evaluate_conditions<";
				oss << pre.size() << ",ability>::condition>";
				generate_condition e_cond(oss);
				std::for_each(pre.begin(), pre.end(), e_cond);
				oss << indent << ")" << std::endl;
				oss << indent << "{" << std::endl;

				generate_logic_fact e_fact(name, oss);
				std::for_each(post.begin(), post.end(), e_fact);

				oss << indent << "}\n\n";

				oss << indent << "void " << exported_name();
				oss << "::async_evaluate_preconditions(model::condition_execution_callback cb)";
				oss << std::endl;
				oss << indent << "{" << std::endl;
				oss << next_indent << "preds.async_compute(cb);\n"; 
				oss << indent << "}" << std::endl;
#if 0
				{
				exec_expression_output e_dump(u, ability_context, *this, oss, tList, "post_");
				std::for_each(post.begin(), post.end(), e_dump);
				}
				oss << indent << "};" << std::endl;
#endif
			}

			std::ostream& operator << (std::ostream& oss, const task& t)
			{
				oss << "Task " << t.get_name() << std::endl;
				oss << "Pre : " << std::endl;
				std::copy(t.pre_begin(), t.pre_end(), 
						std::ostream_iterator<expression_ast>( oss, "\n" ));
				oss << std::endl << "Post : " << std::endl;
				std::copy(t.post_begin(), t.post_end(),
						std::ostream_iterator<expression_ast>( oss, "\n" ));
				return oss;
			}
	}
}
