#include <compiler/extract_unused_result.hh>
#include <compiler/universe.hh>
#include <compiler/recipe_expression.hh>

namespace {
	using namespace hyper::compiler;
struct extract_unused_result_visitor : public boost::static_visitor<void>
{
	std::set<std::string>& list;
	const universe& u;
	const ability& a;
	const symbolList& syms;
	mutable bool catched;

	extract_unused_result_visitor(std::set<std::string>& list, const universe& u,
								 const ability& a, const symbolList& syms) :
		list(list), u(u), a(a), syms(syms), catched(false)
	{}

	/* set_decl, abort does not return anything */
	template <typename T>
	void operator() (const T&) const {}

	void operator() (const let_decl& l) const
	{
		catched = true;
		boost::apply_visitor(*this, l.bounded.expr);
	}

	void operator() (const expression_ast& e) const
	{
		if (!catched) {
			boost::optional<typeId> tid = u.typeOf(a, e, syms); 
			type t = u.types().get(*tid);
			list.insert(t.type_name());
		}
	}

	void operator() (const observer_op<WAIT>& w) const
	{
		if (!catched) 
			list.insert("bool");
		extract_unused_result extract(list, u, a, syms);
		std::for_each(w.content.begin(), w.content.end(), extract);
	}

	void operator() (const observer_op<ASSERT>& w) const
	{
		if (!catched)
			list.insert("hyper::model::identifier");

		extract_unused_result extract(list, u, a, syms);
		std::for_each(w.content.begin(), w.content.end(), extract);
	}

	void operator() (const recipe_op<MAKE>&) const
	{
		if (!catched)
			list.insert("bool");
	}

	void operator() (const recipe_op<ENSURE>&) const
	{
		if (!catched)
			list.insert("hyper::model::identifier");
	}

	void operator() (const abort_decl&) const
	{
		if(!catched) 
			list.insert("boost::mpl::void_");
	}
};
}

namespace hyper { namespace compiler {
	void extract_unused_result::operator() (const recipe_expression& r)
	{
		boost::apply_visitor(extract_unused_result_visitor(list, u, a, syms), r.expr);
	}
}}
