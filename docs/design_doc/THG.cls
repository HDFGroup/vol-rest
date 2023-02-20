%% 
%%   LaTeX class for writing THG documents
%%

%% ========================================================================
%% Admin
%% ========================================================================
\def\fileversion{1.0}
\def\filedate{2014/07/22}
\NeedsTeXFormat{LaTeX2e}
\ProvidesClass{THG}[\filedate\space class by The HDF Group (v\fileversion)]

%% ========================================================================
%% Option parsing
%% ========================================================================
%% Conditional packages (can be disabled)
\def\@empty{}
\def\@yes{yes}
\def\@requirebooktabs{yes}
\def\@requirehyper{}
\def\@requirerfc{}

%% Change footnote markers to alphabetic
\DeclareOption{alphafoot}{%
  \DeclareRobustCommand{\thefootnote}{\alph{footnote}}%
}

% Use hyperref package (with decent options)
\DeclareOption{hyper}{
  \def\@requirehyper{yes}}

% Use hyperref package (with decent options) only if output is PDF
\DeclareOption{hyperpdf}{
  \ifx\pdfoutput\undefined\else%
  \def\@requirehyper{yes}%
  \fi%
}

% Add RFC prefix
\DeclareOption{rfc}{
  \def\@requirerfc{yes}}

%% Don't break enumerations (etc.) across pages in
%% an ugly manner
\clubpenalty = 10000
\widowpenalty = 10000

%% Use nice-looking tables
\DeclareOption{booktabs}{%
  \def\@requirebooktabs{yes}}
\DeclareOption{nobooktabs}{%
  \def\@requirebooktabs{}}

\DeclareOption{letterpaper}{%
  \PassOptionsToClass{letterpaper}{scrartcl}%
}
\DeclareOption{legalpaper}{%
  \PassOptionsToClass{legalpaper}{scrartcl}%
}
\DeclareOption{executivepaper}{%
  \PassOptionsToClass{executivepaper}{scrartcl}%
}

%% Process the options (no package requirement allowed above here)
\ProcessOptions

%% ========================================================================
%% Base class and packages
%% ========================================================================

%% Base class
\LoadClass[11pt, BCOR=0pt, DIV=classic]{scrartcl}

%% For headers (recommended instead of fancyhdr)
\setkomafont{pageheadfoot}{\small}
\RequirePackage{scrlayer-scrpage}

% Conditional packages
\RequirePackage{ifpdf}
%\RequirePackage{ifthen}
\RequirePackage{ifluatex}

% Use subfile package to speed up compilations
% (Note special heading in each subfile)
\RequirePackage{subfiles}

% Use English by default
\RequirePackage[english]{babel}

%% Graphics
\ifpdf
  \RequirePackage[pdftex]{graphicx}
  \DeclareGraphicsExtensions{.pdf,.jpeg,.jpg,.png}
  % For transparency
  \pdfpageattr{/Group <</S /Transparency /I true /CS /DeviceRGB>>}
\else
  \RequirePackage[dvips]{graphicx}
\fi

% Allow images to be in a subdirectory (and relative to
% the path for subfile sections)
\graphicspath{{images/}{../images/}}

% Font
\ifluatex
  \RequirePackage{luatextra, xunicode}
  \defaultfontfeatures{Ligatures=TeX}
  \setmainfont[
    UprightFont={*},
    BoldFont={*-Bold},
    ItalicFont={*-Italic},
    BoldItalicFont={*-BoldItalic},
    SlantedFont={*-Italic},
    BoldSlantedFont={*-BoldItalic}
  ]{CMU Serif}
  \setsansfont[
    UprightFont={*},
    BoldFont={*-Bold},
    ItalicFont={*-Oblique},
    BoldItalicFont={*-BoldOblique}
  ]{CMU Sans Serif}
  \setmonofont[
    UprightFont={*-Regular},
    BoldFont={*-Bold},
    ItalicFont={*-Italic},
    BoldItalicFont={*-BoldItalic}
  ]{CMU Typewriter}
\else
  \RequirePackage{times}
  \RequirePackage[scaled=.95]{helvet}
  \RequirePackage{courier}
  %\linespread{1.25}
  \KOMAoptions{DIV=last}
\fi

%% If building with PDFLaTeX, use microtype spacing adjustments
\RequirePackage{microtype}

%% Need to be able to locally change the text width.
\RequirePackage{changepage}

%% Used to center the variable width page quote (must be version >= 0.9a)
\RequirePackage{varwidth}[2003/03/10]

%% Math font
\RequirePackage{amsmath}
\RequirePackage{mathtools}
\ifluatex
  \RequirePackage{unicode-math, lualatex-math}
  \setmathfont[math-style=ISO, bold-style=ISO]{Asana Math}
\else
  \RequirePackage{amssymb}
  \newcommand{\lgblksquare}{\blacksquare}
\fi
\AtEndOfPackage{%
  %% Displayed maths environments re-defined to be the same as AMS' align
  \renewenvironment{displaymath}{\align}{\endalign\ignorespacesafterend}
  \newenvironment{displaymath*}%
  {\expandafter\let\expandafter\@oldoldalignstar \csname align*\endcsname \@oldoldalignstar}%
  {\expandafter\let\expandafter\@oldoldendalignstar \csname endalign*\endcsname \@oldoldendalignstar \ignorespacesafterend}
  \renewenvironment{equation}{\align}{\endalign\ignorespacesafterend}
  \renewenvironment{equation*}%
  {\expandafter\let\expandafter\@oldoldalignstar \csname align*\endcsname \@oldoldalignstar}%
  {\expandafter\let\expandafter\@oldoldendalignstar \csname endalign*\endcsname \@oldoldendalignstar \ignorespacesafterend}
}%

%% Use the \title and \author arguments to build the front page etc.
%% Re-define \title and \author to store their args in \thetitle, \theauthor variables
\renewcommand*\title[1]{\gdef\@title{#1}\global\let\thetitle\@title}%
\def\theauthors{}
\renewcommand*\author[1]{\gdef\@author{#1}\global\let\theauthor\@author
\g@addto@macro\theauthors{#1 \\}
}%
\renewcommand*\date[1]{\gdef\@date{#1}\global\let\thedate\@date}%
\newcommand*\rfcversion[1]{\gdef\@rfcversion{#1}\global\let\therfcversion\@rfcversion}%

%% Re-define \organization and \orglogo to store their args in \theorganization and \theorglogo
\newcommand*\organization[1]{\gdef\@organization{#1}\global\let\theorganization\@organization}
\newcommand*\orglogo[1]{\gdef\@orglogo{#1}\global\let\theorglogo\@orglogo}

%% Use the \revision argument to build list of revisions.
\def\revisions{}
\newcounter{revision}
% Adds a new revision
\def\revision#1#2#3{\g@addto@macro\revisions{%
\hline {\small #1} & {\small #2} & {\small #3} \\}
}

%% For manpages
\newcommand*\manfunc[1]{\gdef\@manfunc{\detokenize{#1}}\global\let\themanfunc\@manfunc}%
\newcommand*\manpurpose[1]{\gdef\@manpurpose{#1}\global\let\themanpurpose\@manpurpose}%
\newcommand*\mandesc[1]{\gdef\@mandesc{#1}\global\let\themandesc\@mandesc}%
%% Use the \manparam argument to build list of manparam.
\def\manparams{}
\newcounter{manparam}
% Adds a new manparam
\def\manparam#1#2#3{\g@addto@macro\manparams{%
\texttt{\detokenize{#1}} & [{#2}] {#3} \\}
}
\newcommand*\manreturn[1]{\gdef\@manreturn{#1}\global\let\themanreturn\@manreturn}%

%% Nice tables (optional)
\ifx\@requirebooktabs\@empty\else%
  \RequirePackage{booktabs}
  \AtEndOfClass{%
    %% Tabular environment, defined to use the booktabs facilities
    \let\@oldtabular\tabular
    \let\@oldendtabular\endtabular
    \renewenvironment{tabular}[1]{%
      \@oldtabular{@{}#1@{}}%
      %\toprule%
    }{%
      %\bottomrule%
      \@oldendtabular%
      \ignorespacesafterend%
    }
    %%
  }%
\fi
\RequirePackage{tabularx}
\RequirePackage{multirow}

%% The right way to do custom spacing
\RequirePackage{setspace}
%% \singlespacing, \onehalfspacing, \doublespacing
%% \begin{spacing}{2.5} ...

%% Nicer itemize
\def\labelitemi{\tiny$\lgblksquare$}

% For colors
\RequirePackage[table]{xcolor}
\definecolor{rulecolor}{RGB}{36,78,152}%{73,126,191}

% For number of pages
\RequirePackage{lastpage}

%% Verbatim things
\RequirePackage{verbatim}

%% Code listings
\RequirePackage{minted}
%\usemintedstyle{borland}
\usemintedstyle{manni}

% Float package
\RequirePackage{float}

%% Re-defines captions of figures and tables
%% TODO: make configurable / optional?
\RequirePackage[format=hang,labelfont=bf,labelsep=endash,font=small]{caption}

% Subfigures
\RequirePackage[labelfont=bf,labelsep=space,list=true]{subcaption}

%% Hyperlinks (optional, has to come early in the package list)
\ifx\@requirehyper\@empty\else%
\RequirePackage[colorlinks,
  citecolor=rulecolor,%
  filecolor=rulecolor,%
  linkcolor=rulecolor,%
  urlcolor=rulecolor,%
  bookmarks,pdfusetitle]{hyperref}
\fi

%% Glossaries
\RequirePackage[acronym,toc]{glossaries}
\setacronymstyle{long-short}

%% Index of terms
\RequirePackage{imakeidx}

%% References & bibliography support
\RequirePackage{csquotes}
\RequirePackage{biblatex}

%% ========================================================================
%% Length definitions
%% ========================================================================

\setlength{\parindent}{0pt}           % tweak the initial indent in paras
\setlength{\parskip}{5pt}             % tweak the intra-para gap size
\setlength{\headheight}{40pt}
\setlength{\footheight}{40pt}

%% Redefine bits of document shape
%\renewcommand{\topfraction}{0.95}%
%\renewcommand{\bottomfraction}{0.95}%
%\renewcommand{\textfraction}{0.05}%
\renewcommand{\arraystretch}{1.25}%
%\renewcommand{\arrayrulewidth}{0.8pt}%

%% Reset rule widths and the header width
\newcommand{\headwidth}{\textwidth}%
\newcommand{\headrulewidth}{0.5pt}%
\newcommand{\footrulewidth}{0.5pt}%
%\addtolength{\headheight}{2.5pt}%
%\addtolength{\headsep}{20pt}%

%% Figure widths
\newlength{\smallfigwidth}
\newlength{\mediumfigwidth}
\newlength{\largefigwidth}
\newlength{\hugefigwidth}
\setlength{\smallfigwidth}{0.45\textwidth}
\setlength{\mediumfigwidth}{0.6\textwidth}
\setlength{\largefigwidth}{0.75\textwidth}
\setlength{\hugefigwidth}{0.9\textwidth}

%% Figure width aliases
\newlength{\littlefigwidth}
\newlength{\bigfigwidth}
\setlength{\littlefigwidth}{\smallfigwidth}
\setlength{\bigfigwidth}{\largefigwidth}

%% Logo height
\newlength{\logoheight}
\setlength{\logoheight}{34pt}

%% ========================================================================
%% Headers and footers
%% ========================================================================
%% No header or footer on the title page
\AtBeginDocument{\thispagestyle{plain}}
%% Use funky headers and footers
\pagestyle{scrheadings}
\KOMAoptions{headsepline=\headrulewidth:\headwidth,clines}
\KOMAoptions{footsepline=\footrulewidth:\headwidth,clines}

%% Normal headers and footers (headers are all like RH pages for oneside)
%% Format is [plain style]{SCR headings style}
\ihead[\thedate]{\thedate}%
\chead[]{}%
\ifx\@requirerfc\@empty
\ohead[\therfcversion]{\therfcversion}
\else%
\ohead[RFC-\theorganization-\therfcversion]{RFC-\theorganization-\therfcversion}
\fi%

\newcommand*{\logo}{\includegraphics[height=\logoheight]{\theorglogo}}
\ifoot[\logo]{\logo}%
\cfoot[]{}%
\ofoot[\raisebox{22pt}{Page~\thepage~of~\pageref{LastPage}}]{\raisebox{22pt}{Page~\thepage~of~\pageref{LastPage}}}%

%% Rule colors
\setkomafont{headsepline}{\color{rulecolor}}
\setkomafont{footsepline}{\color{rulecolor}}

%% ========================================================================
%% Public environments and commands
%% ========================================================================

%% Makes a title
\DeclareRobustCommand{\titlepage}[2][]{%
  \begingroup%
  \sffamily%
  \begin{singlespacing}
  \begin{center}%
    \vspace*{10pt}{\LARGE\bfseries{\thetitle}}\\%
    \vspace*{20pt}{\bfseries{\theauthors}}%
    {\color{rulecolor}\rule{\headwidth}{\headrulewidth}}%
  \end{center}%
  \end{singlespacing}
  \endgroup%
}
\renewcommand{\maketitle}[1]{\titlepage{}}

%% Environment for abstract
\renewenvironment{abstract}{%
  \vspace*{-7pt}%
}{%
  \ignorespacesafterend%
  \\%
  {\color{rulecolor}\rule{\headwidth}{\headrulewidth}}%
}

%% List of revisions
\DeclareRobustCommand{\makerevisions}[1]{%
  \clearpage%
  \section*{Revision History}%
  \begin{flushleft}%
    \begin{tabularx}{\textwidth}{| l | l | X |}%
      \hline%
      \rowcolor{lightgray!50}%
      {\bfseries\sffamily\small\textcolor{rulecolor}{Version Number}} & {\bfseries\sffamily\small\textcolor{rulecolor}{Date}} & {\bfseries\sffamily\small\textcolor{rulecolor}{Comments}}\\
      \revisions%
      \hline
      %\bottomrule%
    \end{tabularx}%
  \end{flushleft}%
  \clearpage%
}

%% Reference manual
\DeclareRobustCommand{\makemanpage}[1]{%
  \begin{flushleft}%
    \texttt{\themanfunc}%
  \end{flushleft}%
  \paragraph{Purpose:}%
    \begin{flushleft}%
      \themanpurpose%
    \end{flushleft}%
  \paragraph{Description:}%
    \begin{flushleft}%
      \themandesc%
    \end{flushleft}%
  \paragraph{Parameters:}
    \begin{flushleft}%
      \begin{tabularx}{\textwidth}{ll}%
        \manparams%
      \end{tabularx}%
    \end{flushleft}%
  \paragraph{Returns:}
    \begin{flushleft}%
      \themanreturn%
    \end{flushleft}%
    \def\manparams{}
    \setcounter{manparam}{0} 
}

%% Bold inline section heading with a finishing colon
\DeclareRobustCommand{\verysubsection}[1]{{\noindent\textbf{#1:}}}

%% Table environment
\let\@oldtable\table
\let\@oldendtable\endtable
\renewenvironment{table}[1][tbp]{%
  \vspace{0.4cm}%
  \@oldtable[#1]%
  \begin{center}%
}{%
  \end{center}%
  \@oldendtable%
  \ignorespacesafterend%
}

%% Figure environment
\let\@oldfigure\figure
\let\@oldendfigure\endfigure
\renewenvironment{figure}[1][htbp]{
  \@oldfigure[#1]%
  \begin{center}%
}{%
  \end{center}%
  \@oldendfigure%
  \ignorespacesafterend%
}

%% Figure* environment (lots of \expandafter and \csname gubbins reqd... *sigh*)
\expandafter\let\expandafter\@oldfigurestar \csname figure*\endcsname
\expandafter\let\expandafter\@oldendfigurestar \csname endfigure*\endcsname
\renewenvironment{figure*}[1][htbp]{
  \@oldfigurestar[#1]%
  \begin{center}%
}{%
  \end{center}%
  \@oldendfigurestar%
  \ignorespacesafterend%
}

%% In-document references
\DeclareRobustCommand{\Chapter}{Chapter}
\DeclareRobustCommand{\Section}{Section}
\DeclareRobustCommand{\Appendix}{Appendix}
\DeclareRobustCommand{\Figure}{Figure}
\DeclareRobustCommand{\Table}{Table}
\DeclareRobustCommand{\Equation}{equation}
\DeclareRobustCommand{\Reference}{reference}
%% Standard way to refer to a page number
\DeclareRobustCommand{\Page}{page}

%% Reference terms with built-in reference
\DeclareRobustCommand{\ChapterRef}[1]{\Chapter~\ref{#1}}
\DeclareRobustCommand{\SectionRef}[1]{\Section~\ref{#1}}
\DeclareRobustCommand{\AppendixRef}[1]{\Appendix~\ref{#1}}
\DeclareRobustCommand{\FigureRef}[1]{\Figure~\ref{#1}}
\DeclareRobustCommand{\TableRef}[1]{\Table~\ref{#1}}
\DeclareRobustCommand{\EquationRef}[1]{\Equation~\eqref{#1}}
\DeclareRobustCommand{\ReferenceRef}[1]{\Reference~\cite{#1}}
\DeclareRobustCommand{\PageRef}[1]{\Page~\pageref{#1}}

%%% todo notes, idea from
%%% http://midtiby.blogspot.com/2007/09/todo-notes-in-latex.html
%%% ... and further modified by Phil Carns from
%%% http://usefreetools.blogspot.com/2008_04_01_archive.html
%%%
\newcommand{\todo}[1]{
      \addcontentsline{tdo}{todo}{\protect{#1}}
      \marginpar{\colorbox{white!90!black}{\textcolor{red}{
      \parbox{1.7cm}{\scriptsize\bfseries\raggedright #1}
      }}}
}

