#include <stdexcept>

#include <boost/make_shared.hpp>

#include <compiler/ability.hh>
#include <compiler/condition_output.hh>
#include <compiler/depends.hh>
#include <compiler/logic_expression_output.hh>
#include <compiler/exec_expression_output.hh>
#include <compiler/extract_symbols.hh>
#include <compiler/output.hh>
#include <compiler/recipe.hh>
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
		const ability &a;
		std::ostream &oss;
		const universe& u;

		generate_logic_fact(const std::string& name, const ability& a, std::ostream& oss_,
						    const universe& u):
			name(name), a(a), oss(oss_), u(u)
		{}

		void operator() (const expression_ast& e) const
		{
			oss << "\t\t\ta_.logic().engine.add_fact(";
			oss << generate_logic_expression(e, a, u);
			oss << ", " << quoted_string(name) <<  ");" << std::endl;
		}
	};

	struct generate_recipe {
		std::ostream& oss;

		generate_recipe(std::ostream& oss_) : oss(oss_) {}

		void operator() (const boost::shared_ptr<recipe>& r) const
		{
			oss << "\t\t\tadd_recipe(boost::shared_ptr<" << r->exported_name() << ">(new ";
			oss << r->exported_name() << "(a_, *this)));\n";
		}
	};

	struct include_recipe {
		std::ostream& oss;
		std::string abilityName;

		include_recipe(std::ostream& oss_, const std::string& abilityName_) :
			oss(oss_), abilityName(abilityName_) 
		{}

		void operator() (const boost::shared_ptr<recipe>& r) const
		{
			oss << "#include <" << abilityName << "/recipes/";
			oss << r->get_name() << ".hh>\n";
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

	struct same_name {
		std::string name;

		same_name(const std::string& name_): name(name_) {};

		bool operator() (const boost::shared_ptr<recipe>& r) const
		{
			return (r->get_name() == name);
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

			bool task::add_recipe(const recipe& r)
			{
				std::vector<boost::shared_ptr<recipe> >::const_iterator it;
				it = std::find_if(recipes.begin(), recipes.end(), same_name(r.get_name()));

				if (it != recipes.end()) {
					std::cerr << "Can't add the recipe named " << r.get_name();
					std::cerr << " because a recipe with the same already exists" << std::endl;
					return false;
				}

				recipes.push_back(boost::make_shared<recipe>(r));
				return true;
			}

			const recipe& task::get_recipe(const std::string& name) const
			{
				std::vector<boost::shared_ptr<recipe> >::const_iterator it;
				it = std::find_if(recipes.begin(), recipes.end(), same_name(name));
				if (it == recipes.end())
					throw std::runtime_error("Can't find recipe " + name);
				return *(*it);
			}

			recipe& task::get_recipe(const std::string& name)
			{
				std::vector<boost::shared_ptr<recipe> >::iterator it;
				it = std::find_if(recipes.begin(), recipes.end(), same_name(name));
				if (it == recipes.end())
					throw std::runtime_error("Can't find recipe " + name);
				return *(*it);
			}

			void task::dump_include(std::ostream& oss, const universe& u) const
			{
				const std::string indent="\t\t";
				const std::string next_indent = indent + "\t";

				std::string exported_name_big = exported_name();
				std::transform(exported_name_big.begin(), exported_name_big.end(), 
							   exported_name_big.begin(), toupper);
				guards g(oss, ability_context.name(), exported_name_big + "_HH");

				/* Extract local and remote symbols for pre and post-conditions */
				extract_symbols pre_symbols(ability_context), post_symbols(ability_context);
				void (extract_symbols::*f)(const expression_ast& e) = &extract_symbols::extract;
				std::for_each(pre.begin(), pre.end(), boost::bind(f, &pre_symbols, _1));
				std::for_each(post.begin(), post.end(), boost::bind(f, &post_symbols, _1));

				oss << "#include <model/task.hh>" << std::endl;
				oss << "#include <model/evaluate_conditions.hh>" << std::endl;


				namespaces n(oss, ability_context.name());
				oss << indent << "struct ability;" << std::endl;
				oss << indent << "struct " << exported_name();
				oss << " : public model::task {" << std::endl;

				if (!pre.empty()) {
					oss << next_indent << "typedef hyper::model::evaluate_conditions<";
					oss << pre.size() << ", ability, " << pre_symbols.local_with_updater.size() << ", ";
					oss << pre_symbols.remote_vector_type_output(u);
					oss << " > pre_conditions;" << std::endl; 
					oss << next_indent << "pre_conditions preds;" << std::endl;
				}

				if (!post.empty()) {
					oss << next_indent << "typedef hyper::model::evaluate_conditions<";
					oss << post.size() << ", ability, " << post_symbols.local_with_updater.size() << ", ";
					oss << post_symbols.remote_vector_type_output(u);
					oss << " > post_conditions;" << std::endl; 
					oss << next_indent << "post_conditions posts;" << std::endl;
				}

				oss << next_indent << exported_name();
				oss << "(ability& a_);" << std::endl; 
				oss << next_indent;
				oss << "void async_evaluate_preconditions(model::condition_execution_callback cb);";
				oss << std::endl;
				oss << "void async_evaluate_postconditions(model::condition_execution_callback cb);";				
				oss << std::endl;
				oss << indent << "bool has_postconditions() const {" << std::endl;
				oss << next_indent << "return ";
				if (post.empty()) 
					oss << "false";
				else
					oss << "true";
				oss << ";\n" << indent << "}\n";

				oss << indent << "};" << std::endl;
			}

			void task::add_depends(depends& deps, const universe& u) const
			{
				void (*f)(const expression_ast&, const std::string&, 
						  const universe& u, depends&) = &hyper::compiler::add_depends;

				std::for_each(pre.begin(), pre.end(),
							  boost::bind(f ,_1, boost::cref(ability_context.name()),
												 boost::cref(u),
												 boost::ref(deps)));
				std::for_each(post.begin(), post.end(),
							  boost::bind(f ,_1, boost::cref(ability_context.name()),
												 boost::cref(u),
												 boost::ref(deps)));

				std::for_each(recipes.begin(), recipes.end(), 
						boost::bind(&hyper::compiler::recipe::add_depends, _1, boost::ref(deps),
																			  boost::cref(u)));
			}

			void task::dump(std::ostream& oss, const universe& u) const
			{
				const std::string indent="\t\t";
				const std::string next_indent = indent + "\t";
				const std::string next_next_indent = indent + "\t";

				depends deps;
				add_depends(deps, u);
				
				oss << "#include <model/logic_layer.hh>\n";
				oss << "#include <" << ability_context.name();
				oss << "/ability.hh>" << std::endl;
				oss << "#include <" << ability_context.name();
				oss << "/tasks/" << name << ".hh>" << std::endl;

				std::for_each(recipes.begin(), recipes.end(), 
							  include_recipe(oss, ability_context.name()));

				oss << "#include <boost/assign/list_of.hpp>" << std::endl;

				std::for_each(deps.fun_depends.begin(), 
							  deps.fun_depends.end(), dump_depends(oss, "import.hh"));
				oss << std::endl;

				/* Extract local and remote symbols for pre and post-conditions */
				extract_symbols pre_symbols(ability_context), post_symbols(ability_context);
				void (extract_symbols::*f)(const expression_ast& e) = &extract_symbols::extract;
				std::for_each(pre.begin(), pre.end(), boost::bind(f, &pre_symbols, _1));
				std::for_each(post.begin(), post.end(), boost::bind(f, &post_symbols, _1));

				std::string context_name = "hyper::" + ability_context.name() + "::" + exported_name();

				{
				anonymous_namespaces n(oss);
				oss << indent << "using namespace hyper;\n";
				oss << indent << "using namespace hyper::" << ability_context.name() << ";\n";
				exec_expression_output e_dump(ability_context, context_name, oss, 
											   "pre_", pre_symbols.remote,
											   symbolList(u.types()));
				std::for_each(pre.begin(), pre.end(), e_dump);
				}

				{
				anonymous_namespaces n(oss);
				oss << indent << "using namespace hyper;\n";
				oss << indent << "using namespace hyper::" << ability_context.name() << ";\n";
				exec_expression_output e_dump(ability_context, context_name, oss, 
											  "post_", post_symbols.remote,
											  symbolList(u.types()));
				std::for_each(post.begin(), post.end(), e_dump);
				}

				namespaces n(oss, ability_context.name());

				/* Generate constructor */
				oss << indent << exported_name() << "::" << exported_name();
				oss << "(hyper::" << ability_context.name() << "::ability & a_) :" ;
				oss << "model::task(a_, " << quoted_string(name) << ")";
				if (!pre.empty())
				{
				oss << ",\n";
				oss << indent << "preds(a_, \n";
				oss << next_indent << "boost::assign::list_of<hyper::" << ability_context.name(); 
				oss << "::" << exported_name() << "::pre_conditions::condition>\n";
				generate_condition e_cond(oss, "pre", ability_context, u);
				std::for_each(pre.begin(), pre.end(), e_cond);

				if (!pre_symbols.local_with_updater.empty()) {
					oss << next_indent << ", boost::array<std::string, ";
					oss << pre_symbols.local_with_updater.size() << " >(\n";
					oss << pre_symbols.local_list_variables_updated(next_next_indent);
					oss << next_indent << ")\n";
				}

				std::string base_type = "hyper::" + ability_context.name() + "::";
				base_type += exported_name() + "::pre_conditions::remote_values::remote_vars_conf";
				if (!pre_symbols.remote.empty()) {
					oss << next_indent << ", " << base_type << "(\n";
					oss << pre_symbols.remote_list_variables(next_next_indent);
					oss << next_indent << ")\n";
				}

				oss << indent << ")";
				}
				if (!post.empty()) 
				{
				oss << ",\n";
				oss << indent << "posts(a_, \n";
				oss << next_indent << "boost::assign::list_of<hyper::" << ability_context.name();
				oss << "::" << exported_name() << "::post_conditions::condition>\n";
				generate_condition e_cond(oss, "post", ability_context, u);
				std::for_each(post.begin(), post.end(), e_cond);
				if (!post_symbols.local_with_updater.empty()) {
					oss << next_indent << ", boost::array<std::string, ";
					oss << post_symbols.local_with_updater.size() << " >(\n";
					oss << post_symbols.local_list_variables_updated(next_next_indent);
					oss << next_indent << ")\n";
				}

				std::string base_type = "hyper::" + ability_context.name() + "::";
				base_type += exported_name() + "::post_conditions::remote_values::remote_vars_conf";
				if (!post_symbols.remote.empty()) {
					oss << next_indent << ", " << base_type << "(\n";
					oss << post_symbols.remote_list_variables(next_next_indent);
					oss << next_indent << ")\n";
				}

				oss << indent << ")" << std::endl;
				}
				oss << indent << "{" << std::endl;

				std::for_each(recipes.begin(), recipes.end(), generate_recipe(oss));
				generate_logic_fact e_fact(name, ability_context, oss, u);
				std::for_each(post.begin(), post.end(), e_fact);

				oss << indent << "}\n\n";

				oss << indent << "void " << exported_name();
				oss << "::async_evaluate_preconditions(model::condition_execution_callback cb)";
				oss << std::endl;
				oss << indent << "{" << std::endl;
				if (pre.empty()) 
					oss << next_indent << "cb(boost::system::error_code(), hyper::model::conditionV());\n";
				else
					oss << next_indent << "preds.async_compute(cb);\n"; 
				oss << indent << "}" << std::endl;

				oss << indent << "void " << exported_name();
				oss << "::async_evaluate_postconditions(model::condition_execution_callback cb)";
				oss << std::endl;
				oss << indent << "{" << std::endl;
				if (post.empty()) 
					oss << next_indent << "cb(boost::system::error_code(), hyper::model::conditionV());\n";
				else
					oss << next_indent << "posts.async_compute(cb);\n"; 
				oss << indent << "}" << std::endl;
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
