
#ifndef _TYPES_PARSER_HH_
#define _TYPES_PARSER_HH_

#include <iostream>
#include <string>
#include <vector>

#include <boost/variant.hpp>

#include <compiler/symbols_parser.hh>

namespace hyper {
	namespace compiler {

		struct struct_decl {
			std::string name;
			symbol_decl_list vars;
		};

		std::ostream& operator << (std::ostream&, const struct_decl&);


		struct newtype_decl {
			std::string newname;
			std::string oldname;
		};

		std::ostream& operator << (std::ostream&, const newtype_decl&);

		typedef boost::variant<struct_decl, newtype_decl> type_decl;

		struct type_decl_list {
			std::vector < type_decl > l;
		};

		std::ostream& operator <<(std::ostream&, const type_decl_list&);
	};
};

#endif /* _TYPES_PARSER_HH_ */
