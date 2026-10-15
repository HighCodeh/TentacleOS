# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = 'Argtable3'
copyright = '2025, Argtable Project members and individual contributors'
author = 'Tom G. Huang'

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = ['breathe', 'myst_parser']

templates_path = ['_templates']
exclude_patterns = []

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = 'furo'
html_static_path = ['_static']
html_css_files = ['custom.css']
html_theme_options = {
    'light_css_variables': {
        'font-stack': 'Open Sans,-apple-system,BlinkMacSystemFont,Segoe UI,Helvetica,Arial,sans-serif,Apple Color Emoji,Segoe UI Emoji',
    },
}

# -- Options for Breathe extension -------------------------------------------
breathe_projects = { "argtable": 'xml' }
breathe_default_project = "argtable"
breathe_order_parameters_first = True
breathe_domain_by_extension = {"h" : "c"}
breathe_default_members = ('members', 'undoc-members')
breathe_show_define_initializer = True

# -- Options for MyST extension -------------------------------------------
myst_enable_extensions = ['colon_fence']