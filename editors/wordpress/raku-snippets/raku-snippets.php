<?php
/**
 * Plugin Name: Raku Snippets
 * Plugin URI:  https://raku.online
 * Description: Turns Raku code blocks in posts and pages into runnable editors powered by raku.online. Mark a block with the CSS class "raku" (or add a data-raku attribute) and it becomes editable and runnable in the browser.
 * Version:     1.0.0
 * Author:      Andrew Shitov
 * Author URI:  https://andrewshitov.com
 * License:     GPL-2.0-or-later
 * Requires at least: 5.0
 * Requires PHP: 7.0
 */

defined( 'ABSPATH' ) || exit;

define( 'RAKU_SNIPPETS_SRC', 'https://raku.online/raku.js' );

/**
 * Does the current page contain Raku blocks?
 *
 * True for a single post or page whose content has a data-raku attribute,
 * or a <pre>/<code> element with the class "raku" (e.g. a Preformatted or
 * Code block with "raku" in Advanced → Additional CSS class).
 */
function raku_snippets_needed() {
	$needed = false;

	if ( is_singular() ) {
		$post = get_queried_object();
		if ( $post instanceof WP_Post ) {
			$content = $post->post_content;
			$needed  = stripos( $content, 'data-raku' ) !== false
				|| preg_match( '/<(?:pre|code)[^>]+class="[^"]*\braku\b/i', $content );
		}
	}

	return (bool) apply_filters( 'raku_snippets_needed', $needed );
}

add_action( 'wp_enqueue_scripts', function () {
	if ( raku_snippets_needed() ) {
		wp_enqueue_script( 'raku-snippets', RAKU_SNIPPETS_SRC, array(), null, true );
	}
} );

// data-auto="raku" lets raku.js pick up ordinary code blocks with the class
// "raku" (or "language-raku"), not only elements carrying data-raku. The value
// limits it to that one class; raku.js alone would also take perl6 and raku6.
add_filter( 'script_loader_tag', function ( $tag, $handle ) {
	if ( 'raku-snippets' === $handle ) {
		$tag = str_replace( ' src=', ' data-auto="raku" src=', $tag );
	}
	return $tag;
}, 10, 2 );
