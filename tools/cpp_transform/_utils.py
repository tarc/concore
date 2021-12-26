class StopToken:
    """
    Class used to indicate that the AST exploration should completely stop.
    If the visiting function returns an instance of this object, then all the followup nodes will not be explored
    """

    pass


def find_token(kind, text, tokens_list, start_idx=0):
    """
    Find the index of the token with the given kind and text.
    :param kind: The kind of the token we are trying to find
    :param text: The text (or spelling) associated with the token we are trying to find
    :param tokens_list: The list of tokens we are searching into
    :param start_idx: The start index to search from; default=0
    :return: The index of the searched token; -1 if we couldn't find the token.
    """
    n = len(tokens_list)
    for i in range(start_idx, n):
        t = tokens_list[i]
        if t.kind == kind and t.spelling == text:
            return i
    return -1


def find_tokens(expected, tokens_list, start_idx=0):
    """
    Find the starting index of the expected list of tokens.
    :param expected: The list of (kind, text) representing the tokens we are expected to find
    :param tokens_list: The list of tokens we are searching into
    :param start_idx: The start index to search from; default=0
    :return: The index where the expected tokens start in the tokens list; -1 if we could't find the tokens.
    """
    n = len(tokens_list)
    m = len(expected)
    for i in range(start_idx, n - m + 1):
        is_ok = True
        for j in range(m):
            t = tokens_list[i + j]
            ex = expected[j]
            if t.kind != ex[0] or t.spelling != ex[1]:
                is_ok = False
                break
        if is_ok:
            return i
    return -1


def visit_all_ast(node, f):
    """
    Visit all the AST nodes from the translation unit (including nodes from imported headers).
    :param node: The root node from where we start the exploration
    :param f: Functor to be called for the nodes; must match signature (node) -> bool/StopToken

    The functor will be called for each node in the tree, if the exploration is not limited. If the function will return
    False for a node, then the children of that node will not be explored; if True is returned, the children are
    explored.

    If the functor returns a `StopToken` object, then the whole exploration stops.
    """

    def _do_visit(node):
        # Apply the function
        visit_children = f(node)

        # Should we stop?
        if isinstance(visit_children, StopToken):
            return True

        # Visit children
        if visit_children:
            for c in node.get_children():
                should_stop = _do_visit(c)
                if should_stop:
                    return True

    # Start the exploration
    _do_visit(node)


def visit_cur_unit_ast(node, f):
    """
    Visit the AST nodes from the current file (ignoring nodes from imported headers).
    :param node: The root node from where we start the exploration
    :param f: Functor to be called for the nodes; must match signature (node) -> bool/StopToken

    The functor will be called for each node in the tree, if the exploration is not limited. If the function will return
    False for a node, then the children of that node will not be explored; if True is returned, the children are
    explored.

    If the functor returns a `StopToken` object, then the whole exploration stops.

    All the nodes that do not belong to the file of the starting node will be ignored.
    """
    our_file = node.extent.start.file

    def _do_visit(node):
        # Don't visit nodes from other files
        if node.location.file and node.location.file.name != our_file.name:
            return False

        # Apply the function
        visit_children = f(node)

        # Should we stop?
        if isinstance(visit_children, StopToken):
            return True

        # Visit children
        if visit_children:
            for c in node.get_children():
                should_stop = _do_visit(c)
                if should_stop:
                    return True

    # Start the exploration
    _do_visit(node)