#include <sstream>

#include <compiler/types_parser.hh>

using namespace hyper::compiler;

std::ostream& hyper::compiler::operator << (std::ostream& os, 
											const struct_decl& s)
{
	os << "declaring struct " << s.name << " containing : " << std::endl;
	os << s.vars << std::endl;
	return os;
}


std::ostream& hyper::compiler::operator << (std::ostream& os,
										    const newtype_decl& t)
{
	os << "declaring " << t.newname << " synonym of " << t.oldname << std::endl;
	return os;
}

std::ostream& hyper::compiler::operator << (std::ostream& os, const opaque_decl& o)
{
	os << "declaring " << o.name << " as an opaque structure " << std::endl;
	return os;
}

struct print_visitor : public boost::static_visitor<std::string>
{
	template <typename T>
	std::string operator()(const T& s) const
	{
		std::ostringstream oss;
		oss << s;
		return oss.str();
	};
};

std::ostream& hyper::compiler::operator << (std::ostream& os,
											const type_decl_list& l)
{
	std::vector<type_decl>::const_iterator it;
	for (it = l.l.begin(); it != l.l.end(); ++it) 
		os << boost::apply_visitor(print_visitor(), *it);
	return os;
}

