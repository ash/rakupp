# WordPress — runnable Raku snippets

`raku-snippets/` is a small WordPress plugin that turns Raku code blocks in
posts and pages into editors you can run in the browser, powered by
[raku.js](https://raku.online/raku.js) from raku.online.

The page for WordPress users, with the zip to download, is
[raku.online/embed/wordpress](https://raku.online/embed/wordpress/); the
plugin's *Visit plugin site* link goes there. That zip is built from this
directory: after a change here, rebuild it in the raku.online repo.

## Install

Upload `raku-snippets.zip` under *Plugins → Add New → Upload Plugin*, or copy
the `raku-snippets` directory to `wp-content/plugins/`; then activate
**Raku Snippets** under *Plugins*.

## Use

In the block editor, add a *Preformatted* or *Code* block with your program
and type `raku` into *Advanced → Additional CSS class(es)*. In HTML:

```html
<pre class="raku">say "Hello, World!";</pre>
```

Only the class `raku` counts (and `language-raku`, the form Markdown and
syntax highlighters write); a block labelled `perl6` stays plain text. Any
element with a `data-raku` attribute works too, together with the
attributes raku.js understands (`data-run`, `data-stdin`, `data-rows`,
`data-hide`, …; see the header of raku.js):

```html
<pre data-raku data-run>say 0.1 + 0.2 == 0.3;</pre>
```

The script is loaded only on single posts and pages that contain such a block;
other pages are untouched. Themes and plugins can override that decision with
the `raku_snippets_needed` filter.

Requires WordPress 5.0+ and PHP 7.0+. Runs on andrewshitov.com
(WordPress 5.3, PHP 7.2, Twenty Sixteen).
