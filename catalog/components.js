class XmlAjax extends HTMLElement {
    #parser;
    xslDoc;
    #url;

    constructor() {
        super();
        this.#parser = new DOMParser();
    }

    connectedCallback() {
        const xsltString = this.querySelector('script[type="text/xml"]').innerHTML;
        this.xslDoc = this.#parser.parseFromString(xsltString, "text/xml");
        this.innerHTML = 'Loading navigation…';
        if (this.xslDoc.querySelector('parsererror')) {
            this.innerHTML = 'Error parsing navigation template.';
            return;
        }
        this.#url = this.getAttribute('src');
        this.fetchAndTransform();
    }

    async fetchAndTransform() {
        const processor = new XSLTProcessor();
        try {
            processor.importStylesheet(this.xslDoc);
            const response = await fetch(this.#url);
            if (!response.ok) throw new Error(response.statusText);
            const xmlDoc = this.#parser.parseFromString(await response.text(), "text/xml");
            if (xmlDoc.querySelector('parsererror')) throw new Error('Invalid navigation XML.');
            this.innerHTML = '';
            this.appendChild(processor.transformToFragment(xmlDoc, document));
        } catch (error) {
            console.error('Unable to load catalog navigation:', error);
            this.innerHTML = 'Unable to load navigation.';
        }
    }
}

customElements.define('xml-ajax', XmlAjax);
