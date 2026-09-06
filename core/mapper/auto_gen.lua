function _rep(file, outfile, pat, rep)
	local f = io.open(file, 'rt');
	local t = f:read("*a");
	f:close();
	
	t = string.gsub(t, pat, rep);

	local f = io.open(outfile, 'w+');
	f:write(t);
	f:close();

end





for i = 10, 255 do
	_rep('templ.c.tpl', tostring(i)..".c", '@NUM@', tostring(i));
end

