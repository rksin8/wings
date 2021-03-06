#pragma once

#include <deal.II/base/point.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/spirit/include/qi.hpp>
#include <regex>

namespace Parsers {
	using namespace dealii;

  template<typename T>
  std::vector<T> parse_string_list(const std::string list_string,
                                   const std::string delimiter=",")
  {
    std::vector<T> list;
    T item;
    if (list_string.size() == 0) return list;
    std::vector<std::string> strs;
    boost::algorithm::split(strs, list_string, boost::is_any_of(delimiter));

    for (const auto &string_item : strs)
    {
      std::stringstream convert(string_item);
      convert >> item;
      list.push_back(item);
    }
    return list;
  }  // eom


  template<>
  std::vector<bool> parse_string_list(std::string list_string,
                                      std::string delimiter)
  {
    // std::cout << "Parsing bool list" << std::endl;
    std::vector<bool> list;
    bool item;
    if (list_string.size() == 0) return list;
    std::vector<std::string> strs;
    boost::split(strs, list_string, boost::is_any_of(delimiter));

    for (auto &string_item : strs)
    {
      std::istringstream is(string_item);
      is >> std::boolalpha >> item;
      // std::cout << std::endl << string_item << std::endl;
      // std::cout << item << std::endl;
      list.push_back(item);
    }
    return list;
  }  // eom


	// convert string to a base type
	template <typename T>
	T convert(const std::string &str)
	{
    std::stringstream conv(str);
		T result;
		conv >> result;
		return result;
	}  // eom


  template<int dim>
  Tensor<1,dim> convert(const std::vector<double> v)
  {
    AssertThrow(v.size() == dim, ExcDimensionMismatch(v.size(), dim));

    Tensor<1,dim> result;
    for (int i=0; i<dim; ++i)
      result[i] = v[i];

    return result;
  } // eom


  template <int dim>
  std::vector< Point<dim> > parse_point_list(const std::string &str)
  {
    // std::cout << str << std::endl;
    std::vector< Point<dim> > points;
    // std::vector<std::string> point_strings;
    // int point_index = 0;
    unsigned int i = 0;
    // loop over symbols and get strings surrounded by ()
    while (i < str.size())
    {
      if (str.compare(i, 1, "(") == 0)  // if str[i] == "(" -> begin point
        {
          std::string tmp;
          while (i < str.size())
          {
            i++;

            if (str.compare(i, 1, ")") != 0)
              tmp.push_back(str[i]);
            else
              break;
          }
          // Add point
          std::vector<double> coords = parse_string_list<double>(tmp);
          Point<dim> point;
          for (int p=0; p<dim; ++p)
            point(p) = coords[p];
          points.push_back(point);
        }
        i++;
    }

    return points;
  }  // eom


  std::vector<std::string> parse_pathentheses_list(const std::string &str)
  {
    std::vector<std::string> result;
    unsigned int i = 0;
    // loop over symbols and get strings surrounded by ()
    while (i < str.size())
    {
      if (str.compare(i, 1, "(") == 0)  // if str[i] == "(" -> begin point
      {
        std::string tmp;
        while (i < str.size())
        {
          i++;

          if (str.compare(i, 1, ")") != 0)
            tmp.push_back(str[i]);
          else
            break;
       }  // end insize parentheses
       // add what's inside parantheses
       result.push_back(tmp);
      }
      i++;
    }
    return result;
  }  // eom


  std::vector<std::string>
  split_bracket_group(const std::string &text,
                      const std::pair<std::string,std::string> delimiters =
                      std::pair<std::string, std::string> ("(", ")"))
  {
    std::vector<std::string> result;
    unsigned int i = 0;
    // loop over symbols and get strings surrounded by ()
    while (i < text.size())
      {
        if (text.compare(i, 1, delimiters.first) == 0)  // if str[i] == "(" -> begin point
          {
            std::string tmp;
            while (i < text.size())
              {
                i++;

                if (text.compare(i, 1, delimiters.second) != 0)
                  tmp.push_back(text[i]);
                else
                  break;
              }  // end insize parentheses
            // add what's inside parantheses
            result.push_back(tmp);
          }
        i++;
      }
    return result;
  }  // eom

  std::vector<std::string>
  split_ignore_brackets(const std::string &text,
                        const std::string &delimiter = ",",
                        const std::pair<std::string,std::string> brackets =
                        std::pair<std::string, std::string> ("(", ")"))
  {
    std::vector<std::string> result;
    unsigned int i = 0;
    // loop over symbols and get strings surrounded by ()
    std::string tmp;
    bool skip_delimiter = false;

    while (i < text.size())
    {
      if (text.compare(i, 1, brackets.first) == 0)
      {
        tmp.clear();
        skip_delimiter = true;
      }
      else if (text.compare(i, 1, brackets.second) == 0)
      {
        boost::algorithm::trim(tmp);
        result.push_back(tmp);
        skip_delimiter = false;
      }
      else if (text.compare(i, 1, delimiter) == 0 && !skip_delimiter)
      {
        boost::algorithm::trim(tmp);
        result.push_back(tmp);
        tmp.clear();
      }
      else
      {
        tmp.append(1, text[i]);
      }

      i++;
    }
    return result;
  } // eom


  std::string parse_command_line(int argc, char *const *argv) {
    std::string filename;
    if (argc < 2) {
      std::cout << "specify the file name" << std::endl;
      exit(1);
    }

    std::list<std::string> args;
    for (int i=1; i<argc; ++i)
      args.push_back(argv[i]);

    int arg_number = 1;
    while (args.size()){
      if (arg_number == 1)
        filename = args.front();
      args.pop_front();
      arg_number++;
    } // EO while args

    return filename;
  }  // EOM

  bool is_number(const std::string& str)
  {
    try
      {
        boost::lexical_cast<double>(str);
        return true;
      }
    catch(boost::bad_lexical_cast &)
      {
        return false;
      }
  }  // eof


  void strip_comments(std::string &text,
                      const std::string &begin_comment="#",
                      const std::string &end_comment="\n")
  {
    std::regex re(begin_comment + "[^" + end_comment + "]*" + end_comment);
    // std::cout << std::regex_replace(text, re, "");
    text = std::regex_replace(text, re, "\n");
  }


  std::string find_substring(const std::string &text,
                             const std::string  &re_str,
                             const unsigned int cut_prefix=0,
                             const unsigned int cut_suffix=0)
  {
    std::string result;
    // std::regex re(begin + "^(?!" + end + ").*");
    // std::regex re(begin+"[\\s\\S]+?"+end);
    std::regex re(re_str);
    std::sregex_iterator
      sec(text.begin(), text.end(), re),
      sec_end;

    AssertThrow(sec!=sec_end,
                ExcMessage("no match found for\n " + re_str));

    for (; sec!=sec_end; ++sec)
    {
      // std::cout << "shit" << "\n";
      std::smatch match = *sec;
      const auto match_str = match.str();
      // std::cout << match.str() << "\n";
      result += match_str.substr(cut_prefix,
                                  match_str.size()-cut_suffix-cut_prefix);
    }
    // std::cout << std::regex_replace(text, re, "");
    // text = std::regex_replace(text, re, "\n");
    return result;
  }


  std::string find_substring(const std::string &text,
                             const std::string &begin,
                             const std::string &end)
  {
    std::string re_str(begin+"[\\s\\S]+?"+end);
    return find_substring(text, re_str, begin.size(), end.size());

  } // eom

} // end of namespace
